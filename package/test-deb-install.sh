#!/usr/bin/env bash
# Smoke-test a .deb by installing it in a fresh ubuntu:24.04 container.
#
# Usage: test-deb-install.sh <build_dir>
#   build_dir : CMake binary directory containing debbuild/*.deb

set -euo pipefail

BUILD_DIR="${1:?build_dir required}"

DEB=$(ls "${BUILD_DIR}/debbuild/"*.deb 2>/dev/null | head -1)
if [[ -z "${DEB}" ]]; then
    echo "ERROR: no .deb found in ${BUILD_DIR}/debbuild/ — run package-deb first" >&2
    exit 1
fi

echo "Testing: $(basename "${DEB}")"

docker run --rm \
    -v "${DEB}:/pkg/$(basename "${DEB}")" \
    ubuntu:24.04 \
    bash -c "
        set -ex
        dpkg -i /pkg/$(basename "${DEB}") || true

        # Binary installed and executable
        test -x /opt/xrpld/bin/xrpld

        # Config files present
        test -f /opt/xrpld/etc/xrpld.cfg
        test -f /opt/xrpld/etc/validators.txt

        # systemd unit installed
        test -f /usr/lib/systemd/system/xrpld.service

        # tmpfiles config
        test -f /usr/lib/tmpfiles.d/xrpld.conf

        # /usr/bin symlink
        test -L /usr/bin/xrpld

        # logrotate config
        test -f /etc/logrotate.d/xrpld

        echo 'All checks passed.'
    "
