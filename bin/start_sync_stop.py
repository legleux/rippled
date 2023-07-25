#!/usr/bin/env python
"""A script to test rippled in an infinite loop of start-sync-stop.
- Requires Python 3.7+.
- Can be stopped with SIGINT.
"""

import sys

assert sys.version_info >= (3, 7)

import argparse
import asyncio
import configparser
import contextlib
import json
import logging
from logging import INFO
import os
import platform
import shutil
import subprocess
import time
import urllib.error
import urllib.request
from pathlib import Path

from prometheus_client import CollectorRegistry, Gauge, push_to_gateway

# Enable asynchronous subprocesses on Windows. The default changed in 3.8.
# https://docs.python.org/3.7/library/asyncio-platforms.html#subprocess-support-on-windows
if platform.system() == "Windows" and sys.version_info < (3, 8):
    asyncio.set_event_loop_policy(asyncio.WindowsProactorEventLoopPolicy())

DEFAULT_EXE = "rippled"
DEFAULT_CONFIGURATION_FILE = "rippled.cfg"
# Number of seconds to wait before forcefully terminating.
PATIENCE = 120
# Number of contiguous seconds in a sync state to be considered synced.
DEFAULT_SYNC_DURATION = 15
# Number of seconds between polls of state.
DEFAULT_POLL_INTERVAL = 1
SYNC_STATES = ("full", "validating", "proposing")
DEBUG_HIGH = False
DEBUG = True
ITERATIONS = 5
DEFAULT_URL = "127.0.0.1"
DEFAULT_PORT = "5005"
DEFAULT_DOWNTIME = [21, 89, 233]

registry = CollectorRegistry()
sync_time = Gauge("rippled_sync_time", "rippled sync time", registry=registry)
db_size = Gauge("db_size", "rippled db size", registry=registry)
metrics = {"sync_time": sync_time, "db_size": db_size}

def read_config(config_file):
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
    config.read(config_file)
    return config

def to_list(value, separator=","):
    """Parse a list from a delimited string value."""
    return [s.strip() for s in value.split(separator) if s]

def find_config_section_file(section, config_file):
    """Try to figure out what log file the user has chosen. Raises all kinds
    of exceptions if there is any possibility of ambiguity."""
    config = read_config(config_file)
    values = list(config[f"{section}"].keys())
    if len(values) < 1:
        raise ValueError(f"no [{section}] in configuration file: {config_file}")
    if len(values) > 1:
        raise ValueError(f"too many [{section}] in configuration file: {config_file}")
    return values[0]

def find_http_port(args):
    if args.port:
        return int(args.port)
    config_file = args.conf
    config = read_config(config_file)
    names = list(config["server"].keys())
    for name in names:
        server = config[name]
        if "http" in to_list(server.get("protocol", "")):
            return int(server["port"])
    raise ValueError(f'no server in [server] for "http" protocol')

def find_db_path(args):
    config_file = args.conf
    config = read_config(config_file)
    db_path = find_config_section_file("database_path", config_file)
    return db_path

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
    config = read_config(config_file)
    filename = find_config_section_file("database_path", config_file)
    try:
        shutil.rmtree(filename)
    except FileNotFoundError as e:
        logging.error(f"No database at {filename}!")  # TODO: check if dir ??


@contextlib.asynccontextmanager
async def rippled(exe=DEFAULT_EXE, config_file=DEFAULT_CONFIGURATION_FILE):
    """A context manager for a rippled process."""
    # Start the server.
    process = await asyncio.create_subprocess_exec(
        str(exe),
        "--conf",
        str(config_file),
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    logging.info(f"rippled started with pid {process.pid}")
    try:
        yield process
    finally:
        # Ask it to stop.
        logging.debug(f"asking rippled (pid: {process.pid}) to stop")
        start = time.time()
        process.terminate()

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

async def sync(url=DEFAULT_URL, *, duration=DEFAULT_SYNC_DURATION, interval=DEFAULT_POLL_INTERVAL):
    """Poll rippled on an interval until it has been synced for a duration."""
    start = time.perf_counter()
    last_state = None
    while (time.perf_counter() - start) < duration:
        await asyncio.sleep(interval)

        request = urllib.request.Request(
            f"http://127.0.0.1:5005",  # BUG: url is port
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
            await push_db_size()
            if not last_state:
                logging.info(f"server_state: {state}")
                logging.debug("in try:")
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

async def loop(
    test, *, exe=DEFAULT_EXE, config_file=DEFAULT_CONFIGURATION_FILE, downtime=DEFAULT_DOWNTIME):
    """
    Start-test-stop rippled in an infinite loop.

    Moves log to a different file after each iteration.
    """
    log_file = find_config_section_file("debug_logfile", config_file)
    it = 0
    sync_times = []
    await push_sync_time("0")
    logging.info(f"*** {exe} --conf {config_file} ***") # TODO: realpath
    logging.info(f"*** {log_file}***")
    logging.info(f"*** {downtime}s between stop/start cycle ***")

    downtime.insert(0, 0)
    while downtime:
        logging.info(f"*** iteration: {it} ***")
        async with rippled(exe, config_file) as process:
            start = time.perf_counter()
            exited = asyncio.create_task(process.wait())
            tested = asyncio.create_task(test())
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
                assert tested.exception() is None
            end = time.perf_counter()
            sync_time = f"{end - start:.0f}"
            logging.info(f"synced after {sync_time} seconds")

            await push_sync_time(sync_time)
            sync_times.append((downtime.pop(0), sync_time))
        # shutil.move(log_file, f'debug.{it}.log') # TODO: uncomment
        if not downtime:  # stop when no downtimes left
            break
        sleep_time = downtime[0]
        logging.info(f"sleeping {sleep_time} seconds")
        await asyncio.sleep(sleep_time)
        if not downtime:
            break
        it += 1
    return sync_times

async def push_db_size():
    size = get_dir_size(db_path)
    db_size = metrics['db_size']
    db_size.set(size)
    logging.debug(f"pushing db size: {size}") # TODO: set to debug
    push_to_gateway("localhost:9091", job="db_size", registry=registry)

async def push_sync_time(sync_time):
    sync_time_gauge = metrics['sync_time']
    sync_time_gauge.set(sync_time)
    logging.info(f"pushing sync time: {sync_time}")
    push_to_gateway("localhost:9091", job="sync_time", registry=registry)

logging.basicConfig(
    format="%(asctime)s %(levelname)-8s %(message)s",
    level=INFO,
    datefmt="%Y-%m-%d %H:%M:%S",
)

parser = argparse.ArgumentParser(formatter_class=argparse.ArgumentDefaultsHelpFormatter)
parser.add_argument(
    "rippled",
    type=Path,
    nargs="?",
    default=DEFAULT_EXE,
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
    nargs="*",
    default=300,
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

port = find_http_port(args)

if args.debug:
    logging.getLogger().setLevel(logging.DEBUG)

db_path = find_db_path(args)

def test():
    return sync(port, duration=args.duration, interval=args.interval)

if not args.keep_db:
    delete_db(args.conf)
try:
    synch_times = asyncio.run(
        loop(test, exe=args.rippled, config_file=args.conf, downtime=args.downtime),
        debug=DEBUG_HIGH,
    )
    print(synch_times)
    # open("sync_times.txt", "w").write("downtime - sync time\n")
    with open("sync_times.txt", 'a') as sync_file:
        for line in synch_times:
            sync_file.write(f"{line}\n")
except KeyboardInterrupt:
    # Squelch the message. This is a normal mode of exit.
    pass
