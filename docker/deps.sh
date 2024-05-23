#!/usr/bin/env bash

apt-get update && apt-get install --yes python3-pip python-is-python3

pip install "conan<2"
pip install cmake
