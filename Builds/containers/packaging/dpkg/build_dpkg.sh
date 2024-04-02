#!/usr/bin/env bash
set -ex

# make sure pkg source files are up to date with repo
# cd /opt/rippled_bld/pkg
cp -fpru ../Builds/containers/packaging/dpkg/debian/. debian/
cp -fpu ../Builds/containers/shared/rippled.service debian/
cp -fpu ../Builds/containers/shared/update_sources.sh .
source update_sources.sh

# Build the dpkg

#dpkg uses - as separator, so we need to change our -bN versions to tilde
RIPPLED_DPKG_VERSION=$(echo "${RIPPLED_VERSION}" | sed 's!-!~!g')
# TODO - decide how to handle the trailing/release
# version here (hardcoded to 1). Does it ever need to change?
RIPPLED_DPKG_FULL_VERSION="${RIPPLED_DPKG_VERSION}-1"


git archive --format tar.gz --prefix rippled-${RIPPLED_DPKG_VERSION}/ -o rippled-${RIPPLED_DPKG_VERSION}.tar.gz HEAD
ln -s ./rippled-${RIPPLED_DPKG_VERSION}.tar.gz rippled_${RIPPLED_DPKG_VERSION}.orig.tar.gz
tar xvf rippled-${RIPPLED_DPKG_VERSION}.tar.gz
cd rippled-${RIPPLED_DPKG_VERSION}
cp -pr ../pkgs/debian .


NOWSTR=$(TZ=UTC date -R)
cat << CHANGELOG > ./debian/changelog
rippled (${RIPPLED_DPKG_FULL_VERSION}) unstable; urgency=low

  * see RELEASENOTES

 -- Ripple Labs Inc. <support@ripple.com>  ${NOWSTR}
CHANGELOG

# PATH must be preserved for our more modern cmake in /opt/local
# TODO : consider allowing lintian to run in future ?
export DH_BUILD_DDEBS=0
#debuild --no-lintian --preserve-envvar PATH --preserve-env -us -uc
dpkg-buildpackage -b -uc -us
