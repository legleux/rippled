#!/usr/bin/env sh

set -x

install_from=$1
use_private=${2:-0} # this option not currently needed by any CI scripts,
                    # reserved for possible future use

if [ "$use_private" -gt 0 ] ; then
    REPO_ROOT="https://rippled:${ARTIFACTORY_DEPLOY_KEY}@${ARTIFACTORY_HOST}/artifactory"
else
    REPO_ROOT="${PUBLIC_REPO_ROOT}"
fi

. ./gitlab-ci/get_component.sh

. /etc/os-release
case ${ID} in
    ubuntu|debian)
        pkgtype="dpkg"
        ;;
    fedora|centos|rhel|rocky|almalinux)
        pkgtype="rpm"
        ;;
    *)
        echo "unrecognized distro!"
        exit 1
        ;;
esac

pkg_dir=build/${pkgtype}/packages
echo "pkg_dir is $pkg_dir"
echo "$pkg_dir contains:"
ls $pkg_dir
echo "build_vars contains:"
cat ${pkg_dir}/build_vars

# this script provides info variables about rippled and package version
. "${pkg_dir}/build_vars"

# Update the OS
if [ "${pkgtype}" = "dpkg" ] ; then
    # sometimes update fails and requires a cleanup
    updateWithRetry()
    {
        if ! apt-get -y update ; then
            rm -rvf /var/lib/apt/lists/*
            apt-get -y clean
            apt-get -y update
        fi
    }

    if [ "${install_from}" = "repo" ] ; then
        apt-get -y upgrade
        updateWithRetry
        apt-get -y install \
            apt \
            apt-transport-https \
            ca-certificates \
            coreutils \
            gnupg \
            util-linux \
            wget

        gpg_key="ripple.asc"
        keyfile_dir="/etc/apt/keyrings"
        keyfile_path="${keyfile_dir}/${gpg_key}"
        mkdir -p "${keyfile_dir}"
        wget -q -O - "${REPO_ROOT}/api/gpg/key/public" > "${keyfile_path}"
        echo "deb [signed-by=$keyfile_path] ${REPO_ROOT}/${DEB_REPO} ${DISTRO} ${COMPONENT}" >> /etc/apt/sources.list
        updateWithRetry
        # uncomment this next line if you want to see the available package versions
        # apt-cache policy rippled
        apt-get -y install rippled=${dpkg_full_version}
    elif [ "${install_from}" = "local" ] ; then
        # cached pkg install
        updateWithRetry
        apt-get -y install libprotobuf-dev libprotoc-dev protobuf-compiler libssl-dev
        rm -f ${pkg_dir}/rippled-dbgsym*.*
        dpkg --no-debsig -i ${pkg_dir}/*.deb
    else
        echo "unrecognized pkg source!"
        exit 1
    fi
elif [ "${pkgtype}" = "rpm" ] ; then
    pkg_manager="dnf"
    ${pkg_manager} -y update
    if [ "${install_from}" = "repo" ] ; then
        pkgs=("yum-utils coreutils util-linux")
        case "$ID" in
            rocky|almalinux)
                pkgs="${pkgs[@]/coreutils}"
        esac
        ${pkg_manager} install -y $pkgs
        REPOFILE="/etc/yum.repos.d/artifactory.repo"
        echo "[Artifactory]" > ${REPOFILE}
        echo "name=Artifactory" >> ${REPOFILE}
        echo "baseurl=${REPO_ROOT}/${RPM_REPO}/${COMPONENT}/" >> ${REPOFILE}
        echo "enabled=1" >> ${REPOFILE}
        echo "gpgcheck=0" >> ${REPOFILE}
        echo "gpgkey=${REPO_ROOT}/${RPM_REPO}/${COMPONENT}/repodata/repomd.xml.key" >> ${REPOFILE}
        echo "repo_gpgcheck=1" >> ${REPOFILE}
        ${pkg_manager} -y update
        # uncomment this next line if you want to see the available package versions
        # {yum} --showduplicates list rippled
        ${pkg_manager} -y install ${rpm_version_release}
    elif [ "${install_from}" = "local" ] ; then
        rm -f ${pkg_dir}/rippled-debug*.rpm
        rm -f ${pkg_dir}/rippled-devel*.rpm
        rm -f ${pkg_dir}/*.src.rpm
        rpm -i ${pkg_dir}/*.rpm
    else
        echo "unrecognized pkg source!"
        exit 1
    fi
else
    echo "unrecognized pkgtype ${pkgtype}!"
    exit 1
fi

# verify installed version
INSTALLED=$(/opt/ripple/bin/rippled --version | awk '{print $NF}')
if [ "${rippled_version}" != "${INSTALLED}" ] ; then
    echo "INSTALLED version ${INSTALLED} does not match ${rippled_version}"
    exit 1
fi

# run unit tests
if [ $ID = "fedora" ]; then
    # TODO: Remove this when the [OpenSSL issue is resolved](https://ripplelabs.atlassian.net/browse/RIPD-3277)
    sed -i 's/^\(config_diagnostics *= *\)1$/\10/' /etc/ssl/openssl.cnf
fi
/opt/ripple/bin/rippled --unittest --unittest-jobs $(nproc) > unittest_results || true

num_failures=$(tail unittest_results -n1 | grep -oP '\d+(?= failures)')
if [ "$num_failures" -ne 0 ]; then
    echo "$num_failures tests failed!"
    CONTENT=$(cat <<EOF
payload={
  "username": "GitlabCI",
  "text": "rippled failed ${num_failures} unittests on ${ID} ${VERSION_ID}\nResults at ${CI_JOB_URL}",
  "icon_emoji": ":boom:"}
EOF
)
    cat unittest_results
    curl "$SLACK_NOTIFY_URL" --data-urlencode "$CONTENT"
    exit 1
fi

/opt/ripple/bin/validator-keys --unittest > vkt_unittest_results || true
vkt_num_failures=$(tail vkt_unittest_results -n1 | grep -oP '\d+(?= failures)')
if [ "$vkt_num_failures" -ne 0 ]; then
    echo "$vkt_num_failures tests failed!"
    CONTENT=$(cat <<EOF
payload={
  "username": "GitlabCI",
  "text": "validator-keys failed ${vkt_num_failures} unittests on ${ID} ${VERSION_ID}\nResults at ${CI_JOB_URL}",
  "icon_emoji": ":boom:"}
EOF
)
    cat vkt_unittest_results
    curl "$SLACK_NOTIFY_URL" --data-urlencode "$CONTENT"
    exit 1
fi
