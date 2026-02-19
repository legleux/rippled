


Final procedure

Use CMake to populate a staging root in `debian/tmp` via the CMake `DESTDIR` environment variable.
`$ENV{DESTDIR}`

Building the package
```
#
cmake -S . -B build -DCMAKE_INSTALL_PREFIX=/

DESTDIR="$PWD/debian/tmp" cmake --install build
```
Additional requirements in build images:
```
dh-sequence-systemd
dh-sequence-sysusers
dh-sequence-tmpfiles
```

`debian/xrpld.service`
`debian/xrpld.tmpfiles` (→ `dh_installtmpfiles`)
`debian/xrpld.sysusers` (→ `dh_installsysusers`)
`debian/xrpld.links` (→ `dh_link`)
optionally `debian/rippled.logrotate` (→ `dh_installlogrotate`)


## Smoke build

Set the environment
```
export DEB_BUILD_OPTIONS="noautodbgsym reproducible=+fixfilepath"
export SOURCE_DATE_EPOCH=$(git log -1 --pretty=%ct 2>/dev/null || date +%s)
export TZ=UTC
export LC_ALL=C.UTF-8
export LANG=C.UTF-8
export UMASK=0022
# deterministic compression. As deb files are tarballs, compression may have timestamps to break reproducibility.
export GZIP=-n
export XZ_OPT="--threads=0"
```
Then it said use
```
export SOURCE_DATE_EPOCH=$(git log -1 --pretty=%ct || date +%s)
export TZ=UTC
export LC_ALL=C.UTF-8
export GZIP=-n
```


Explanation

SOURCE_DATE_EPOCH	canonical build timestamp
TZ	removes timezone variance
LC_ALL	stable sorting / text output
UMASK	deterministic file permissions
DEB_BUILD_OPTIONS	enables reproducibility fixes in tools

If only packaging the build/install tree use

`dpkg-buildpackage -us -uc -b --buildinfo-option=-O0`
INSTALL_TREE=/path/to/installroot dpkg-buildpackage -us -uc -b
A rules file that does the full build would run
`dpkg-buildpackage -us -uc -b`

## After the build

Check the helpers ran
`dpkg-deb -c ../rippled_*.deb | grep systemd`

Should produce:
```
lib/systemd/system/rippled.service
usr/lib/sysusers.d/rippled.conf
usr/lib/tmpfiles.d/rippled.conf
```

`dpkg-deb -c ../rippled*.deb | grep sysusers`
Should show `usr/lib/sysusers.d/rippled.conf`

After install, ensure that
`getent passwd rippled`
returns
`rippled:x:123:123:XRPL Daemon:/var/lib/rippled:/usr/sbin/nologin`


## linting
Run it after building the .deb, from the directory containing it.
`lintian ../rippled_*.deb` or the stricter `lintian -I --pedantic ../rippled_*.deb`


## Test reproducibility locally

Build twice
```
dpkg-buildpackage -us -uc -b
mv ../rippled*.deb /tmp/a.deb

dpkg-buildpackage -us -uc -b
mv ../rippled*.deb /tmp/b.deb

cmp /tmp/a.deb /tmp/b.deb && echo OK
```

`diffoscope /tmp/a.deb /tmp/b.deb`
