#!/usr/bin/env sh
set -ex

export DEBIAN_FRONTEND="noninteractive"
apt-get --yes update
apt-get --yes install \
    curl \
    git \
    gnupg \
    software-properties-common \
    tzdata

curl -sk -o rippled-pubkeys.txt "${GIT_SIGN_PUBKEYS_URL}"
gpg --import rippled-pubkeys.txt

if git verify-commit HEAD; then
    echo "git commit signature check passed"
else
    echo "git commit signature check failed"
    git log -n 5 --color \
        --pretty=format:'%Cred%h%Creset -%C(yellow)%d%Creset %s %Cgreen(%cr) %C(bold blue)<%an> [%G?]%Creset' \
        --abbrev-commit
    exit 1
fi
