#!/usr/bin/env bash

set -o errexit
set -o xtrace

. /etc/os-release

case "$ID $ID_LIKE" in
  *rhel*|*fedora*)
    # dnf -y module install "nodejs:20/common"
    PKG="rpm"
    ;;
  *debian*|*ubuntu*)
    # curl -fsSL https://deb.nodesource.com/setup_20.x -o nodesource_setup.sh
    # chmod +x nodesource_setup.sh
    # ./nodesource_setup.sh
    # apt-get install -y nodejs
    PKG="deb"
    ;;
esac

# build_dir="/root/build/${PKG}/packages"
# ./pkgs/build_rippled.${PKG}.sh

# echo "my build_vars" > build_vars
# exit 0
# if [ 1 -eq 0 ]; then
repo_dir=$PWD
set -a
repo_name="rippled"
pkgs_dir="${repo_dir}/pkgs"
shared_files="${pkgs_dir}/shared"
pkg_files="${pkgs_dir}/packaging/${PKG}"
build_info_src="${repo_dir}/src/libxrpl/protocol/BuildInfo.cpp"
xrpl_version=$(grep -E -i -o "\b(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(-[0-9a-z\-]+(\.[0-9a-z\-]+)*)?(\+[0-9a-z\-]+(\.[0-9a-z\-]+)*)?\b" "${build_info_src}")

git config --global --add safe.directory '*'
branch=$(git rev-parse --abbrev-ref HEAD)
commit=$(git rev-parse HEAD)
short_commit=$(git rev-parse --short=7 HEAD)
date=$(git show -s --format=%cd --date=format-local:"%Y%m%d%H%M%S")

conan_remote_name="${CONAN_REMOTE_NAME:-xrplf}"
conan_remote_url="${CONAN_REMOTE_URL:-https://conan.ripplex.io}"

BUILD_TYPE=${BUILD_TYPE:-Release}
set +a

if [ "${branch}" = 'develop' ]; then
    # TODO: Can remove when CMake sets version
    dev_version="${date}~${short_commit}"
    xrpl_version="${xrpl_version}+${dev_version}"
fi

if [ "${PKG}" = 'rpm' ]; then
    IFS='-' read -r RIPPLED_RPM_VERSION RELEASE <<< "${xrpl_version}"
    export RIPPLED_RPM_VERSION
    RPM_RELEASE=${RPM_RELEASE-1}
    # post-release version
    if [ "hf" = "$(echo "$RELEASE" | cut -c -2)" ]; then
        RPM_RELEASE="${RPM_RELEASE}.${RELEASE}"
    # pre-release version (-b or -rc)
    elif [[ $RELEASE ]]; then
        RPM_RELEASE="0.${RPM_RELEASE}.${RELEASE}"
    fi
    export RPM_RELEASE

    if [[ $RPM_PATCH ]]; then
        RPM_PATCH=".${RPM_PATCH}"
        export RPM_PATCH
    fi

    build_dir="build/${PKG}/packages"
    rm -rf ${build_dir}
    mkdir -p ${build_dir}/rpmbuild/{BUILD,RPMS,SOURCES,SPECS,SRPMS}
    cp "${pkgs_dir}/rippled.patch" ${build_dir}/rpmbuild/SOURCES/
    git archive \
        --remote "${repo_dir}" HEAD \
        --prefix ${repo_name}/ \
        --format tar.gz \
        --output ${build_dir}/rpmbuild/SOURCES/rippled.tar.gz
    ln --symbolic "${repo_dir}" ${build_dir}/rippled
    cp -r "${pkgs_dir}/packaging/rpm/rippled.spec" ${build_dir}
    pushd "${build_dir}" || exit

    rpmbuild \
        --define "_topdir ${PWD}/rpmbuild" \
        --define "_smp_build_ncpus %(nproc --ignore=2 2>/dev/null || echo 1)" \
        -ba rippled.spec

    RPM_VERSION_RELEASE=$(rpm -qp --qf='%{NAME}-%{VERSION}-%{RELEASE}' ./rpmbuild/RPMS/x86_64/rippled-[0-9]*.rpm)
    tar_file=$RPM_VERSION_RELEASE.tar.gz

    printf '%s\n' \
        "rpm_md5sum=$(rpm -q --queryformat '%{SIGMD5}\n' -p ./rpmbuild/RPMS/x86_64/rippled-[0-9]*.rpm 2>/dev/null)" \
        "rpm_sha256=$(sha256sum ./rpmbuild/RPMS/x86_64/rippled-[0-9]*.rpm | awk '{ print $1 }')" \
        "rippled_version=${xrpl_version}" \
        "rippled_git_hash=${commit}" \
        "rpm_version=${RIPPLED_RPM_VERSION}" \
        "rpm_file_name=${tar_file}" \
        "rpm_version_release=${RPM_VERSION_RELEASE}" \
        > build_vars

    # Rename the files to match the debs
    mv rpmbuild/RPMS/x86_64/* .
    for f in *x86_64.rpm; do
      new="${f/x86_64/amd64}"
      mv "$f" "$new"
      echo "Renamed $f -> $new"
    done
    rm -rf rpmbuild
    rm -f rippled rippled.tar.gz rippled.spec
    pushd -0 && dirs -c

    mv "${build_dir}/build_vars" .

elif [ "${PKG}" = 'deb' ]; then
    dpkg_version=$(echo "${xrpl_version}" | sed 's:-:~:g')
    full_version="${dpkg_version}"
    build_dir="build/${PKG}/packages"
    rm -rf ${build_dir}
    mkdir -p ${build_dir}

    git archive \
        --remote "${repo_dir}" HEAD \
        --prefix ${repo_name}/ \
        --format tar.gz \
        --output "${build_dir}/${repo_name}_${dpkg_version}.orig.tar.gz"

    pushd ${build_dir} || exit
    tar -zxf "${repo_name}_${dpkg_version}.orig.tar.gz"

    pushd ${repo_name} || exit

    # Prepare the package metadata directory, `debian/`, within `rippled/`.
    cp -r "${pkg_files}/debian" .
    cp  "${shared_files}/rippled.service" debian/
    cp  "${shared_files}/update-rippled.sh" .
    cp  "${shared_files}/update-rippled-cron" .
    cp  "${shared_files}/rippled-logrotate" .

    if [ "${branch}" = 'develop' ]; then
        # TODO: Can remove when CMake sets version
        sed --in-place "s/versionString = \"\([^\"]*\)\"/versionString = \"${xrpl_version}\"/" "${build_info_src}"
    fi

    cat << CHANGELOG > ./debian/changelog
rippled (${xrpl_version}) unstable; urgency=low

    * see RELEASENOTES

    -- Ripple Labs Inc. <support@ripple.com>  $(TZ=UTC date -R)
CHANGELOG

    cat ./debian/changelog
    dpkg-buildpackage -b -d -us -uc

    popd || exit
    rm -rf ${repo_name}
    # for f in *.ddeb; do mv -- "$f" "${f%.ddeb}.deb"; done
    popd || exit
    cp ${build_dir}/${repo_name}_${xrpl_version}_amd64.changes .

    awk '/Checksums-Sha256:/{hit=1;next}/Files:/{hit=0}hit' ${repo_name}_${xrpl_version}_amd64.changes | sed -E 's!^[[:space:]]+!!' > shasums
    sha() {
        <shasums awk "/$1/ { print \$1 }"
    }

    printf '%s\n' \
        "deb_sha256=$(sha "rippled_${full_version}_amd64.deb")" \
        "dbg_sha256=$(sha "rippled-dbgsym_${full_version}_amd64.deb")" \
        "rippled_version=${xrpl_version}" \
        "rippled_git_hash=${commit}" \
        "dpkg_version=${dpkg_version}" \
        "dpkg_full_version=${full_version}" \
        > build_vars

    pushd -0 && dirs -c
fi

# fi

# find . -name "*.${PKG}"
# mkdir -p $build_dir
# if [ "${PKG}" = 'rpm' ]; then
#     mv /root/rpmbuild/RPMS/x86_64/* .
#     for f in *x86_64.rpm; do
#       new="${f/x86_64/amd64}"
#       mv "$f" "$build_dir/$new"
#       echo "Renamed $f -> $new"
#     done
#     # mv /root/rpmbuild/RPMS/x86_64/* $build_dir/
# else
#     echo $PWD
#     find / -name "rippled-3.0.0_amd64.deb"
#     mv *.deb $build_dir
# fi
# printf '%s\n' \
#     "rippled_version=3.0.0" \
#     "rippled_git_hash=deadbeef" \
#     > build_vars
cp "${build_dir}/"*.$PKG .
