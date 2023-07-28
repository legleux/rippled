import configparser
import os
import yaml
from pathlib import Path

ROOT_DIR = os.path.dirname(os.path.realpath(__file__))
CONFIG_FILE = Path(ROOT_DIR) / 'config.yml'

SERVICE_FILE_NAME = "muh.service"
DESCRIPTION = "Log rippled sync times"
SYSTEMD_DIR = "/etc/systemd/system"


def read_service_params():
    with open(CONFIG_FILE) as config:
        config = yaml.safe_load(config)
    return config['service']


def write_service_file(output_path, service_params):
    service_file = Path(output_path) / SERVICE_FILE_NAME
    params = {
        "Unit": {"Description": DESCRIPTION},
        "Service": {
            "User": service_params['user'],
            "Restart": "always",
            "ExecStart": service_params['script_path'],
            "TimeoutStopSec": service_params['shutdown_timeout']
        },
        "Install": {
            "WantedBy": "multi-user.target"
        }
    }

    config = configparser.ConfigParser()
    config.optionxform = str
    config.update(params)

    with open(service_file, 'w') as sf:
        config.write(sf, space_around_delimiters=False)


write_service_file(SYSTEMD_DIR, read_service_params())
