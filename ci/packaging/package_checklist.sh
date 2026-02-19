#!/bin/bash

# Control file parses
dpkg-parsechangelog > /dev/null

# Required packaging files exist
for f in control rules changelog rippled.service rippled.sysusers rippled.links; do
  test -f debian/$f || echo "missing debian/$f"
done

# rules is executable
test -x debian/rules || chmod +x debian/rules

# Install tree sanity
test -d "$INSTALL_TREE/opt" || echo "missing opt/"
test -d "$INSTALL_TREE/etc" || echo "missing etc/"

# Install tree not polluted
test ! -d "$INSTALL_TREE/usr/local" || echo "BAD: contains usr/local"

# Sysusers file syntax OK
grep -q '^u ' debian/rippled.sysusers || echo "sysusers missing user line"

# Build deps present
dpkg-checkbuilddeps

# Debhelper version supports compat 13
dh --version

# Clean previous artifacts
rm -rf debian/tmp debian/.debhelper debian/*.substvars

# Build
# INSTALL_TREE=/path/to/installroot dpkg-buildpackage -us -uc -b
