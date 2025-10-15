#!/usr/bin/env bash

mkdir -p rippled/usr/local/bin
mkdir rippled/DEBIAN

cat >rippled/usr/local/bin/rippled <<'EOF'
#!/bin/sh
echo "Hello, from rippled!"
EOF
chmod 755 rippled/usr/local/bin/rippled

arch=$(dpkg-architecture -qDEB_BUILD_ARCH)
version="3.0.0"

cat >rippled/DEBIAN/control <<EOF
Package: rippled
Version: $version
Section: base
Priority: optional
Architecture: $arch
Maintainer: You <you@example.com>
Description: Simple hello world
EOF

pkg_name="rippled-${version}_${arch}.deb"
dpkg-deb --build rippled $pkg_name

dpkg -i $pkg_name
