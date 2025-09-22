#!/usr/bin/env bash
set -eo pipefail
set -x
# set -u
# shopt -s extglob

. /etc/os-release

PACKAGE=rippled
BRANCH=stable
VERSION="" # Default to the latest version
TEST=""    # Default to production repos

RIPPLE_KEY_URL="https://repos.ripple.com/repos/api/gpg/key/public"

usage() {
    cat <<EOF
Usage: $0 [-b BRANCH] [-v VERSION] [-t TEST]
Defaults:
    branch=$BRANCH
    version=${VERSION:-uses the latest version available}
    test=${TEST:-uses the production repo}
EOF
}

    while getopts ":b:v:t:h" opt; do
      case $opt in
        b) BRANCH=$OPTARG ;;
        v) VERSION=$OPTARG ;;
        t) TEST=$OPTARG ;;
        h) usage; exit 0 ;;
        \?) echo "Unknown option: -$OPTARG" >&2; usage; exit 2 ;;
        :)  echo "Option -$OPTARG requires an argument" >&2; usage; exit 2 ;;
      esac
    done
    shift $((OPTIND-1))

case "$BRANCH" in
stable|master)
    suite=stable
    ;;
unstable|release)
    suite=unstable
    ;;
nightly|develop)
    suite=nightly
    ;;
esac

if [[ $(id -u) -ne 0 ]]; then
    sudo_cmd="sudo"
else
    sudo_cmd=""
fi

case "$ID $ID_LIKE" in
  *rhel*|*fedora*)
    : echo "Red Hat-like"
    pkg_type="rpm"
    pkg_mgr_install=(dnf install -y)
    ;;
  *debian*|*ubuntu*)
    : echo "Debian-ish"
    pkg_type="deb"
    pkg_mgr_install=(apt-get install -y)
    ;;
esac

ripple_repo="ripple-${pkg_type}"
if [[ -n ${TEST:-} ]]; then
    ripple_repo="${ripple_repo}-test-mirror"
fi

install_cmd() {
    $sudo_cmd "${pkg_mgr_install[@]}" "$@"
}

install_rhel_source(){
    cat << REPOFILE | tee /etc/yum.repos.d/ripple.repo
[ripple-${suite}]
name=XRP Ledger Packages
enabled=1
gpgcheck=0
repo_gpgcheck=1
baseurl=https://repos.ripple.com/repos/rippled-rpm/${suite}/
gpgkey=https://repos.ripple.com/repos/rippled-rpm/${suite}/repodata/repomd.xml.key
REPOFILE
}

install_ripple_key(){
    $sudo_cmd install -m 0755 -d /etc/apt/keyrings && \
        wget -qO- "${RIPPLE_KEY_URL}" | \
        $sudo_cmd gpg --dearmor -o /etc/apt/keyrings/ripple.gpg
}

deb_prereq(){
    $sudo_cmd apt-get update && \
        $sudo_cmd apt-get install -y --no-install-recommends \
            ca-certificates \
            wget \
            gnupg
}

install_debian_source(){
    echo "deb [signed-by=/etc/apt/keyrings/ripple.gpg] https://repos.ripple.com/repos/rippled-deb ${VERSION_CODENAME} ${suite}" | \
        $sudo_cmd tee -a /etc/apt/sources.list.d/ripple.list
    $sudo_cmd apt-get update
}

enable_service(){
    if command -v systemctl
    then
        $sudo_cmd systemctl daemon-reload
        $sudo_cmd systemctl enable "${PACKAGE}.service"
        $sudo_cmd systemctl start "${PACKAGE}.service"
    fi
}

case "$pkg_type" in
  rpm)
    install_rhel_source
    if [[ -n ${VERSION:-} ]]; then
        package="${PACKAGE}-${VERSION}"
    else
        package="${PACKAGE}"
    fi
    ;;
  deb)
    deb_prereq
    install_ripple_key
    install_debian_source
    if [[ -n ${VERSION:-} ]]; then
        package="${PACKAGE}=${VERSION}-1"
    else
        package="${PACKAGE}"
    fi
    ;;
esac

# : echo install_cmd "${package}"
install_cmd "${package}"

if [ "${PACKAGE}" = "rippled" ]; then
    version=$(rippled --version | head -n 1 | awk  '{print $3}')
elif [ "${PACKAGE}" = "clio" ]; then
    echo "cut clio_server version output"
fi

echo "Installed ${PACKAGE} ${version}"
