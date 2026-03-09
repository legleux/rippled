#!/usr/bin/env bash
# Build an RPM or Debian package from a pre-built xrpld binary.
#
# Usage: build-pkg.sh <pkg_type> <src_dir> <build_dir> [version]
#   pkg_type  : rpm | deb
#   src_dir   : path to repository root (CMAKE_SOURCE_DIR)
#   build_dir : path to CMake build directory (CMAKE_BINARY_DIR)
#   version   : package version string (required for deb; e.g. 2.4.0-b1)

set -euo pipefail

PKG_TYPE="${1:?pkg_type required}"
SRC_DIR="${2:?src_dir required}"
BUILD_DIR="${3:?build_dir required}"

case "${PKG_TYPE}" in
rpm)
    set -x
    mkdir -p \
        "${BUILD_DIR}/rpmbuild/BUILD" \
        "${BUILD_DIR}/rpmbuild/BUILDROOT" \
        "${BUILD_DIR}/rpmbuild/RPMS" \
        "${BUILD_DIR}/rpmbuild/SOURCES" \
        "${BUILD_DIR}/rpmbuild/SPECS" \
        "${BUILD_DIR}/rpmbuild/SRPMS"

    cp "${BUILD_DIR}/package/rpm/xrpld.spec" \
        "${BUILD_DIR}/rpmbuild/SPECS/xrpld.spec"

    cp "${SRC_DIR}/package/shared/xrpld.service" \
        "${BUILD_DIR}/rpmbuild/SOURCES/xrpld.service"
    cp "${SRC_DIR}/package/shared/xrpld.sysusers" \
        "${BUILD_DIR}/rpmbuild/SOURCES/xrpld.sysusers"
    cp "${SRC_DIR}/package/shared/xrpld.tmpfiles" \
        "${BUILD_DIR}/rpmbuild/SOURCES/xrpld.tmpfiles"

    rpmbuild -bb \
        --define "_topdir ${BUILD_DIR}/rpmbuild" \
        --define "cmake_build_dir ${BUILD_DIR}" \
        "${BUILD_DIR}/rpmbuild/SPECS/xrpld.spec"
    ;;

deb)
    VERSION="${4:-1.0.0}"
    STAGING="${BUILD_DIR}/debbuild/source"

    rm -rf "${STAGING}"
    mkdir -p "${STAGING}"

    # Source files referenced by debian/rules
    cp "${SRC_DIR}/README.md"  "${STAGING}/"
    cp "${SRC_DIR}/LICENSE.md" "${STAGING}/"

    # debian/ control files
    cp -r "${SRC_DIR}/package/deb/debian" "${STAGING}/debian"

    # Shared support files for dh_installsystemd/sysusers/tmpfiles
    cp "${SRC_DIR}/package/shared/xrpld.service"  "${STAGING}/debian/xrpld.service"
    cp "${SRC_DIR}/package/shared/xrpld.sysusers" "${STAGING}/debian/xrpld.sysusers"
    cp "${SRC_DIR}/package/shared/xrpld.tmpfiles"  "${STAGING}/debian/xrpld.tmpfiles"

    # debian/changelog is required by dpkg-buildpackage; generate a minimal one.
    # Pre-release versions use ~ instead of - (e.g. 2.4.0-b1 → 2.4.0~b1).
    DEB_VERSION="${VERSION//-/\~}"
    cat > "${STAGING}/debian/changelog" <<EOF
xrpld (${DEB_VERSION}-1) unstable; urgency=medium

  * Release ${VERSION}.

 -- XRPL Foundation <contact@xrpl.org>  $(LC_ALL=C date -u -R)
EOF

    set -x
    cd "${STAGING}"
    CMAKE_BUILD_DIR="${BUILD_DIR}" dpkg-buildpackage -b --no-sign
    ;;

*)
    echo "Unknown package type: ${PKG_TYPE}" >&2
    exit 1
    ;;
esac
