# Linux Packaging

This directory contains all files needed to build RPM and Debian packages for `xrpld`.

## Directory layout

```
package/
  build-pkg.sh      Script called by CMake targets to drive the build tool
  rpm/
    xrpld.spec.in   RPM spec template (CMake substitutes @xrpld_version@)
  deb/
    debian/         Debian control files (control, rules, install, links, logrotate)
  shared/
    xrpld.service   systemd unit file (used by both RPM and DEB)
    xrpld.sysusers  sysusers.d config (used by both RPM and DEB)
    xrpld.tmpfiles  tmpfiles.d config (used by both RPM and DEB)
```

## Prerequisites

Both package types require a pre-built `xrpld` binary in the CMake build directory, produced by a prior `cmake --build` step with `-Dxrpld=ON`.

| Package type | Build container                           | Tool required                                                  |
| ------------ | ----------------------------------------- | -------------------------------------------------------------- |
| RPM          | `ghcr.io/xrplf/ci/rhel-9:gcc-12-*`        | `rpmbuild`                                                     |
| DEB          | `ghcr.io/xrplf/ci/debian-bookworm:gcc-12` | `dpkg-buildpackage`, `debhelper (= 13)`, `dh-sequence-systemd` |

## Building packages via CMake

Configure with the required install prefix, then invoke the target:

```bash
cmake \
  -DCMAKE_INSTALL_PREFIX=/opt/xrpld \
  -Dxrpld=ON \
  -Dtests=OFF \
  ..

# RPM (in RHEL container):
cmake --build . --target package-rpm

# DEB (in Debian container):
cmake --build . --target package-deb
```

The `cmake/XrplPackaging.cmake` module gates each target on whether the required
tool (`rpmbuild` / `dpkg-buildpackage`) is present at configure time, so
configuring on a host that lacks one simply omits the corresponding target.

`CMAKE_INSTALL_PREFIX` must be `/opt/xrpld`; if it is not, both targets are
skipped with a `STATUS` message.

## How `build-pkg.sh` works

`build-pkg.sh <pkg_type> <src_dir> <build_dir> [version]` is invoked by the
CMake custom targets. CMake passes `CMAKE_SOURCE_DIR`, `CMAKE_BINARY_DIR`, and
(for DEB) `xrpld_version`.

### RPM

1. Creates the standard `rpmbuild/{BUILD,BUILDROOT,RPMS,SOURCES,SPECS,SRPMS}` tree inside the build directory.
2. Copies the generated `xrpld.spec` (from `CMAKE_BINARY_DIR/package/rpm/`) and the shared support files into `SPECS/` and `SOURCES/`.
3. Runs `rpmbuild -bb` with `cmake_build_dir` defined so the spec can call `cmake --install`.
4. Output: `rpmbuild/RPMS/x86_64/xrpld-*.rpm`

### DEB

1. Creates a staging source tree at `debbuild/source/` inside the build directory.
2. Copies `README.md` and `LICENSE.md` (referenced by `debian/rules`).
3. Copies `package/deb/debian/` control files into `debbuild/source/debian/`.
4. Copies shared service/sysusers/tmpfiles into `debian/` where `dh_installsystemd`, `dh_installsysusers`, and `dh_installtmpfiles` pick them up automatically.
5. Generates a minimal `debian/changelog` (pre-release versions use `~` instead of `-`).
6. Runs `dpkg-buildpackage -b --no-sign` with `CMAKE_BUILD_DIR` set so `debian/rules` can call `cmake --install`.
7. Output: `debbuild/*.deb` (one level above the staging source root)

## Post-build verification

```bash
# DEB
dpkg-deb -c debbuild/*.deb | grep -E 'systemd|sysusers|tmpfiles'
lintian -I debbuild/*.deb

# RPM
rpm -qlp rpmbuild/RPMS/x86_64/*.rpm
```

## Reproducibility

The following environment variables improve build reproducibility. They are not
set automatically by `build-pkg.sh`; set them manually if needed:

```bash
export SOURCE_DATE_EPOCH=$(git log -1 --pretty=%ct)
export TZ=UTC
export LC_ALL=C.UTF-8
export GZIP=-n
export DEB_BUILD_OPTIONS="noautodbgsym reproducible=+fixfilepath"
```

## TODO

- Port debsigs signing instructions and integrate into CI.
- Port RPM GPG signing setup (key import + `%{?_gpg_sign}` in spec).
- Introduce a virtual package for key rotation.

## Historical notes

`ci/packaging/package_checklist.sh` and `ci/packaging/final_packaging_improvements.md`
were development scratch files used during initial prototyping. They are
superseded by this README, `build-pkg.sh`, and the reusable CI workflows
(`reusable-package-rpm.yml`, `reusable-package-deb.yml`).
