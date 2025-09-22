#!/usr/bin/env bash

# set -x

BRANCH=stable
# The location of the repo files
url="http://localhost:8000/"
. /etc/os-release

REPO_BASE_URL="https://repos.ripple.com/repos"

case "$ID $ID_LIKE" in
  *rhel*|*fedora*)
    echo "Red Hat-like"
    repo_file="ripple.repo"
    url="${url}/${repo_file}"
    repo_path="/etc/yum.repos.d/${repo_file}"
    REPO="${REPO_BASE_URL}/rippled-rpm/${BRANCH}"
    RIPPLE_RPM_KEY_URL="${REPO}/repodata/repomd.xml.key"
    KEY_URL="${RIPPLE_RPM_KEY_URL}"

    # dnf install cur
    ;;
  *debian*|*ubuntu*)
    echo "debian-ish"
    repo_file="ripple.sources"
    url="${url}/${repo_file}"
    repo_path="/etc/apt/sources.list.d/${repo_file}"
    REPO="${REPO_BASE_URL}/rippled-deb"
    KEY_URL="${RIPPLE_DEB_KEY_URL}"
    RIPPLE_DEB_KEY_URL="${REPO_BASE_URL}/api/gpg/key/public"
    KEY_PATH="/etc/apt/keyrings/ripple.gpg"
    apt-get update && \
    apt-get install -y --no-install-recommends \
        ca-certificates \
        wget \
        curl \
        gnupg
    if [ -f "${KEY_PATH}" ]; then
        echo "Ripple's GPG key already installed"
        # if # sha doesn't matches, update
    else
        install -m 0755 -d /etc/apt/keyrings && \
            wget -qO- "${RIPPLE_DEB_KEY_URL}" | \
            gpg --dearmor -o "${KEY_PATH}"
    fi
    ;;
esac

curl -fsSL "${url}" \
    | sed -e "s|\$BRANCH|${BRANCH}|g" \
    | sed -e "s|\$REPO|${REPO}|g" \
    | sed -e "s|\$KEY_URL|${KEY_URL}|g" \
    | sed -e "s|\$VERSION_CODENAME|${VERSION_CODENAME}|g" \
    | sed -e "s|\$KEY_PATH|${KEY_PATH}|g" \
    > "$repo_path"
