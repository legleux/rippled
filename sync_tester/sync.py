#!/usr/bin/env python
import argparse
import asyncio
import configparser
import contextlib
import json
import logging
import os
import platform
import shutil
import subprocess
import sys
import time
import urllib.error
import urllib.request
import yaml
from datetime import datetime, timedelta
from pathlib import Path

from ps_mem import getMemStats
from prometheus_client import Gauge, start_http_server

"""A script to test rippled in an infinite loop of start-sync-stop.
- Requires Python 3.7+.
- Can be stopped with SIGINT.
"""
logging.basicConfig(
    format="%(asctime)s %(levelname)-8s %(message)s",
    level=logging.DEBUG,
    datefmt="%Y-%m-%d %H:%M:%S",
)
assert sys.version_info >= (3, 7)
# Enable asynchronous subprocesses on Windows. The default changed in 3.8.
# https://docs.python.org/3.7/library/asyncio-platforms.html#subprocess-support-on-windows
if platform.system() == "Windows" and sys.version_info < (3, 8):
    asyncio.set_event_loop_policy(asyncio.WindowsProactorEventLoopPolicy())

ROOT_DIR = os.path.dirname(os.path.realpath(__file__))

CONFIG_FILE = Path(ROOT_DIR) / 'config.yml'
DEFAULT_EXE = "rippled"
DEFAULT_CONFIGURATION_FILE = "rippled.cfg"
# Number of seconds to wait before forcefully terminating.
PATIENCE = 120
# Number of contiguous seconds in a sync state to be considered synced.
DEFAULT_SYNC_DURATION = 5
# Number of seconds between polls of state.
DEFAULT_POLL_INTERVAL = 1
SYNC_STATES = ("full", "validating", "proposing")
ITERATIONS = 5
DEFAULT_URL = "127.0.0.1"
DEFAULT_PORT = "5005"
DEFAULT_DOWNTIME = 60
LOG_DIR = f'{ROOT_DIR}/logs/{datetime.now().strftime("%Y-%m-%d_%H-%M-%S")}'

SYNC_TIME_FILE = 'sync_times.txt'
DATE_FORMAT = "%m/%d/%Y %H:%M:%S"

metrics = {
    "sync_time": Gauge("rippled_sync_time", "rippled sync time"),
    "db_size": Gauge("db_size", "rippled db size"),
    "mem_usage": Gauge("mem_usage", "rippled RAM usage"),
    "server_state": Gauge("server_state", "rippled server_state")
}

server_states = {
    "empty": 0,
    "connected": 1,
    "syncing": 2,
    "full": 3,
    "disconnected": 4,
    "stopped": 5
}

pid = None


def read_config():
    with open(CONFIG_FILE) as config:
        config = yaml.safe_load(config)
    return config


config = read_config()
# rippleds = [(rippled_flr, rippled_flr_cfg), (rippled_develop, rippled_dev_cfg)]


def read_rippled_config(config_file):

    # strict = False: Allow duplicate keys, e.g. [rpc_startup].
    # allow_no_value = True: Allow keys with no values. Generally, these
    # instances use the "key" as the value, and the section name is the key,
    # e.g. [debug_logfile].
    # delimiters = ('='): Allow ':' as a character in Windows paths. Some of
    # our "keys" are actually values, and we don't want to split them on ':'.
    config = configparser.ConfigParser(
        strict=False,
        allow_no_value=True,
        delimiters=("="),
    )
    config.optionxform = str
    config.read(config_file)
    return config


def to_list(value, separator=","):
    """Parse a list from a delimited string value."""
    return [s.strip() for s in value.split(separator) if s]


def find_config_section_file(section, config_file):
    """Try to figure out what log file the user has chosen. Raises all kinds
    of exceptions if there is any possibility of ambiguity."""
    logging.info(f"Looking for config file at: {config_file}")  # debug log
    config_file = Path(config_file)
    if not config_file.is_file():
        logging.error(f"{config_file} doesn't exist!")
        exit(1)

    config = read_rippled_config(config_file)

    values = list(config[f"{section}"].keys())
    if len(values) < 1:
        raise ValueError(f"no [{section}] in configuration file: {config_file}")
    if len(values) > 1:
        raise ValueError(f"too many [{section}] in configuration file: {config_file}")
    return values[0]


def find_http_port(args, config_file):
    if args.port:
        return int(args.port)
    # config_file = args.conf
    logging.info(f"using config file: {config_file}")
    config = read_rippled_config(config_file)
    names = list(config["server"].keys())
    for name in names:
        server = config[name]
        if "http" in to_list(server.get("protocol", "")):
            return int(server["port"])
    raise ValueError('no server in [server] for "http" protocol')


# def find_db_path(args):
#     # config_file = args.conf
#     config = read_rippled_config(config_file)  # TODO: fix this db finding to be more (less?) globally accessible
#     db_path = find_config_section_file("database_path", config_file)
#     return db_path


def get_dir_size(path):
    total = 0
    with os.scandir(path) as it:
        for entry in it:
            if entry.is_file():
                total += entry.stat().st_size
            elif entry.is_dir():
                total += get_dir_size(entry.path)
    logging.debug(f"{path} size = {total}")
    return total


def delete_db(config_file):
    # config = read_rippled_config(config_file)
    filename = find_config_section_file("database_path", config_file)
    try:
        shutil.rmtree(filename)
    except FileNotFoundError:
        logging.error(f"No database at {filename}!")  # TODO: check if dir ??


def rippled_version(exe, config_file):
    output = subprocess.check_output([exe, "--conf", config_file, "--version"])
    version = output.decode().strip().split('version')[1]
    return version


@contextlib.asynccontextmanager
async def rippled(exe=DEFAULT_EXE, config_file=None):
    """A context manager for a rippled process."""
    logging.debug(f"exe: {exe}")  # debug
    logging.debug(f"config_file: {config_file}")
    # Start the server.
    process = await asyncio.create_subprocess_exec(
        str(exe),
        "--conf",
        str(config_file),
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    logging.debug(f"rippled PID {process.pid}")
    global pid
    pid = process.pid
    try:
        yield process
    finally:
        # Ask it to stop.
        logging.debug(f"asking rippled (pid: {process.pid}) to stop")
        start = time.time()
        try:
            process.terminate()
        except ProcessLookupError:
            logging.error(f"rippled {pid} already exited!")
        # Wait nicely.
        try:
            await asyncio.wait_for(process.wait(), PATIENCE)
        except asyncio.TimeoutError:
            # Ask the operating system to kill it.
            logging.warning(f"killing rippled ({process.pid})")

            try:
                process.kill()
            except ProcessLookupError:
                pass

        code = await process.wait()
        end = time.time()
        logging.info(f"rippled stopped after {end - start:.1f} seconds with code {code}")
        await push_state(server_states['stopped'])
        await reset_mem_gauge()


async def sync(url=DEFAULT_URL, *, duration=DEFAULT_SYNC_DURATION, interval=DEFAULT_POLL_INTERVAL):
    """Poll rippled on an interval until it has been synced for a duration."""
    start = time.perf_counter()
    last_state = None
    # await push_metric('sync_time', '0') # initialize sync time
    while (time.perf_counter() - start) < duration:
        await asyncio.sleep(interval)

        request = urllib.request.Request(
            "http://127.0.0.1:5005",  # BUG: url is port
            data=json.dumps({"method": "server_state"}).encode(),
            headers={"Content-Type": "application/json"},
        )
        with urllib.request.urlopen(request) as response:
            try:
                body = json.loads(response.read())
            except urllib.error.HTTPError as cause:
                logging.warning(f"server_state returned not JSON: {cause}")
                start = time.perf_counter()
                continue
        try:
            state = body["result"]["state"]["server_state"]
            # await push_db_size()
            await push_mem(pid)
            await push_state(server_states[state])

            if not last_state:
                logging.info(f"server_state: {state}")
                logging.debug(f"state {state}")
                logging.debug(f"last_state {last_state}")
                last_state = state

        except KeyError as cause:
            logging.warning(f"server_state response missing key: {cause.key}")
            start = time.perf_counter()
            continue

        if state != last_state:
            logging.debug("state != last_state")
            logging.debug(f"last_state {last_state}")
            logging.debug(f"current state {state}")
            logging.info(f"server_state: {state}")
            logging.debug(f"setting last_state to {state}")
            last_state = state

        elif state == last_state:
            logging.debug(f"State: {state} - No state change")

        if state not in SYNC_STATES:
            # Require a contiguous sync state.
            start = time.perf_counter()


async def loop(test, downtime=DEFAULT_DOWNTIME):
    """
    Start-test-stop rippled in an infinite loop.
    Moves log to a different file after each iteration.
    """
    config = read_config()
    rippleds = config['binaries']
    number_of_builds = len(rippleds)
    it = 0
    sync_times = []
    logging.info(f"*** {downtime}s between stop/start cycle ***")
    rippled_labels = list(map(lambda test_exe: test_exe['label'], config['binaries']))
    logging.info(f"*** Comparing {' vs '.join(rippled_labels)} ***")
    logging.debug(f"Config:\n{json.dumps(config, indent=2)}")
    next_start_time = {label: "" for label in rippled_labels}
    while True:
        binary_index = it % number_of_builds
        rippled_test = rippleds[binary_index]
        label, exe, config_file = rippled_test.values()
        # build_version = label
        restart_time = next_start_time[label]

        log_file = find_config_section_file("debug_logfile", config_file)
        minutes = "%H:%M:%S"
        if restart_time:
            wait = 300  # TODO: How to wait nicer
            while datetime.now() < restart_time:
                logging.info(f"We're still before {restart_time.strftime(minutes)}... checking again in {wait}s!")
                await asyncio.sleep(wait)
        logging.info(f"*** Iteration: {it} ***")
        logging.info(f"*** {exe} --conf {config_file} ***")  # TODO: realpath
        logging.info(f"*** Log file:{log_file} ***")
        # ver = rippled_version(exe, config_file) # TODO: change to label
        # logging.info(f"*** rippled version: {ver} ***")
        async with rippled(exe, config_file) as process:
            start = time.perf_counter()
            exited = asyncio.create_task(process.wait())
            tested = asyncio.create_task(test(config_file))
            # Try to sync as long as the process is running.
            done, pending = await asyncio.wait(
                {exited, tested},
                return_when=asyncio.FIRST_COMPLETED,
            )
            if done == {exited}:
                code = exited.result()
                logging.warning(f"server halted for unknown reason with code {code}")
            else:
                assert done == {tested}
                try:
                    assert tested.exception() is None, f"tested.exception: {tested.exception()}"
                except AssertionError as e:
                    logging.error(e)
                    raise
            end = time.perf_counter()
            sync_time = f"{end - start:.0f}"

            now = datetime.now()
            wait_time = timedelta(seconds=downtime)
            logging.info(f"At {now} {exe} synced after {sync_time} seconds")
            next_start_time.update({f"{label}": now + wait_time})
            next_start_time_formatted = next_start_time[label].strftime(DATE_FORMAT)
            logging.info(f"Will start {label} again after {wait_time} {next_start_time_formatted}")
            await push_sync_time(sync_time)
            await write_sync_time(label, sync_time, SYNC_TIME_FILE)
            # TODO: Should sync times for each build be separate? I think yes
        log_file_dir = f'{LOG_DIR}/{label}'
        log_file_path = f'{log_file_dir}/debug.{it}.log'
        if not Path(log_file_dir).is_dir():
            os.makedirs(log_file_dir)
        shutil.move(log_file, log_file_path)
        it += 1
    return sync_times

# async def push_db_size(): # TODO: make this a param
#     db_size = get_dir_size(db_path)
#     await push_metric('db_size', db_size)


async def push_sync_time(sync_time):
    await push_metric('sync_time', sync_time)


async def reset_mem_gauge():
    await push_mem()


async def push_mem(pid=0):
    mem_usage = getMemStats(pid)[0] if pid else 0
    await push_metric('mem_usage', mem_usage)


async def push_state(server_state):
    await push_metric('server_state', server_state)


async def push_metric(metric_key, metric):
    if metric_key != 'server_state':
        metric_str = metric
    else:
        metric_str = f"{metric} [{list(server_states.keys())[list(server_states.values()).index(1)]}]"  # TODO: Ugly
    logging.debug(f"Setting {metric_key}: {metric_str}")
    gauge = metrics[metric_key]
    gauge.set(metric)
    if metric_key == 'sync_time':
        logging.info(f"{metric_key}: {metric}")
    try:
        #  push_to_gateway("localhost:9091", job=metric_key, registry=registry)
        pass
    except urllib.error.URLError as e:
        logging.error(f"Couldn't push {metric_key} to prometheus.\nError: {e}")


async def write_sync_time(build_version, sync_time, sync_times_file):
    now = datetime.now()
    log_message = f"{now}: {sync_time}"
    # sync_times_file_path = Path(sync_times_file)
    sync_time_file_path = f'{LOG_DIR}/{build_version}'
    Path(sync_time_file_path).mkdir(parents=True, exist_ok=True)
    sync_time_file = Path(f'{sync_time_file_path}/{sync_times_file}')
    try:
        # TODO: if db doesn't exist, not if the sync file doesn't exist, it's the initial sync
        # or make the "build_version" the key of the "rippleds" dict and add a "synced" key to the dict that is set
        # after initial sync
        if not sync_time_file.is_file():
            log_message = f"Initial sync for {build_version}\n{log_message} seconds"

        with open(sync_time_file, 'a') as sync_file:
            # TODO: what to log the rippled instance as? write a map/legend at top of file?
            sync_file.write(f"{log_message}\n")
    except FileNotFoundError:
        logging.error(f"Couldn't open {sync_times_file}")
    logging.debug(f"Wrote {log_message} to {sync_times_file}")


parser = argparse.ArgumentParser(formatter_class=argparse.ArgumentDefaultsHelpFormatter)

parser.add_argument(
    "rippled",
    type=Path,
    nargs="?",
    default=None,
    help="Path to rippled.",
)
parser.add_argument(
    "--conf",
    type=Path,
    default=DEFAULT_CONFIGURATION_FILE,
    help="Path to configuration file.",
)
parser.add_argument(
    "--url",
    type=str,
    default=DEFAULT_URL,
    help="rippled url",
)
parser.add_argument(
    "--port",
    type=str,
    default=DEFAULT_PORT,
    help="rippled port",
)
parser.add_argument(
    "--duration",
    type=int,
    default=DEFAULT_SYNC_DURATION,
    help="Number of contiguous seconds required in a synchronized state.",
)
parser.add_argument(
    "--interval",
    type=int,
    default=DEFAULT_POLL_INTERVAL,
    help="Number of seconds to wait between polls of state.",
)
parser.add_argument(
    "--iterations",
    type=int,
    default=ITERATIONS,
    help="Number of iterations of synching to run.",
)
parser.add_argument(
    "--downtime",
    type=int,
    # nargs="*",
    default=DEFAULT_DOWNTIME,
    help="Number of seconds to wait before restarting.",
)
parser.add_argument(
    "--debug",
    action="store_true",
    help="debug",
)
parser.add_argument(
    "--keep-db",
    action="store_true",
    help="keep db",
)
args = parser.parse_args()


if args.debug:
    logging.getLogger().setLevel(logging.DEBUG)

# db_path = find_db_path(args)


def test(config_file):
    port = find_http_port(args, config_file)
    return sync(port, duration=args.duration, interval=args.interval)


if not args.keep_db:
    #  delete_db(args.conf)
    pass

try:
    start_http_server(8000)
    asyncio.run(loop(test, downtime=args.downtime))

except KeyboardInterrupt:
    # Squelch the message. This is a normal mode of exit.
    pass
