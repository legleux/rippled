
`dpkg-parsechangelog`
`dpkg-gencontrol`


make a changelog

`dch --create` # requires `devscripts` package

debian version needs - replaced in pre-release
y=${x//-/\~}

`rippled-3.2.0-b0` becomes `rippled-3.2.0~b0-1` This is the package name and what goes in the changelog file.

1) Clean any previous staging output
rm -rf debian/tmp debian/.debhelper debian/rippled debian/files


(Does not touch your build artifacts.)

1) Reconfigure to fix CMAKE_INSTALL_PREFIX
cmake -S . -B build -DCMAKE_INSTALL_PREFIX=/

2) Rebuild (safe even if nothing changes)
cmake --build build -j"$(nproc)"

3) Install into the Debian staging root (DESTDIR)
cmake --install build --prefix=/ --destdir="$PWD/debian/tmp"


Sanity check that you are not installing into usr/local:

test ! -d debian/tmp/usr/local || (echo "ERROR: installed into /usr/local" >&2; exit 1)
find debian/tmp -maxdepth 3 -type d | sed -n '1,50p'




You should see debian/tmp/opt/ripple/... and debian/tmp/etc/opt/ripple/....
4) Build the binary package

If you have a proper debian/changelog already:

dpkg-buildpackage -us -uc -b

If you prefer a quicker local build (still uses debhelper):

debuild -us -uc -b

5) Inspect the resulting .deb

ls -1 ../*.deb
dpkg-deb -c ../rippled_*_*.deb | sed -n '1,120p'
dpkg-deb -c ../rippled_*_*.deb | grep -E 'systemd|sysusers|tmpfiles|logrotate|/etc/opt/ripple|/opt/ripple|/usr/bin'

6) Lintian (optional but useful)

lintian -I --pedantic ../rippled_*_*.deb

#################################
Build ferreal
#################################

## Install the deps
sudo apt-get install -y \
  debhelper \
  dh-sequence-systemd \
  dpkg-dev

optionally
`apt-get install -y devscripts lintian`
Holy shit that installs a lot (211 packages)!

From the dir that contains debian/ dir.

## Fake a changelog

cat > debian/changelog <<EOF
rippled (3.1.0-1) unstable; urgency=medium

  * Initial package of upstream beta build.

 -- Michael Legleux <mlegleux@rippled.com>  $(LC_ALL=C date -u -R)
EOF

```
dpkg-parsechangelog >/dev/null && echo "changelog OK"
changelog OK
```

## sanity

`dpkg-query -W debhelper debhelper-compat dh-sequence-systemd`
debhelper should be >= 13

## Build

#GnuInstallDirs sets INSTALL_PREFIX to `/usr/local` as if you're installing for development. We don't want that so we
# need to reconfigure CMake (no cache deletion needed).
`cmake -S . -B build -DCMAKE_INSTALL_PREFIX=/`

`cmake --install build --prefix=/ --destdir="$PWD/installroot"`

`INSTALL_TREE=/path/to/installroot dpkg-buildpackage -us -uc -b`
