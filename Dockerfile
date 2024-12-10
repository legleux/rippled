# FROM debian:buster-slim as packager
FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive
RUN echo "alias l='ls -lh'" > ~/.bashrc

RUN apt-get update && apt-get install -y \
    ca-certificates \
    curl \
    gpg \
    jq \
    rpm \
    python-is-python3 \
    python3-pip \
    vim

# Dev helpers tree
RUN apt-get update && apt-get install -y \
    cmake \
    tree
RUN pip install cupcake "conan<2"
