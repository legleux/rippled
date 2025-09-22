#!/usr/bin/env sh

set -o errexit
set -o xtrace

action=$1
filter=$2

source ./gitlab-ci/get_component.sh

apk add \
    coreutils \
    curl \
    jq \
    util-linux

TOPDIR=$(pwd)

# DPKG
ARTIFACTORY_URL="https://${ARTIFACTORY_HOST}artifactory"

debian_distros="bullseye bookworm trixie"
ubuntu_distros="focal jammy noble"
supported_deb_distros="${debian_distros} ${ubuntu_distros}"

cd $TOPDIR
cd build/dpkg/packages
. ./build_vars

CURLARGS="--fail -sk -X${action} --header \"X-JFrog-Art-Api: ${ARTIFACTORY_DEPLOY_KEY}\""
RIPPLED_PKG=$(ls rippled_*.deb)
RIPPLED_DBG_PKG=$(ls rippled-dbgsym_*.*deb)

DEB_MATRIX=";deb.component=${COMPONENT};deb.architecture=amd64"
for dist in ${supported_deb_distros} ; do
    DEB_MATRIX="${DEB_MATRIX};deb.distribution=${dist}"
done
echo "{ \"debs\": {" > "${TOPDIR}/files.info"
for deb in ${RIPPLED_PKG} ${RIPPLED_DBG_PKG}; do
    # first item doesn't get a comma separator
    if [ $deb != $RIPPLED_PKG ] ; then
        echo "," >> "${TOPDIR}/files.info"
    fi
    echo "\"${deb}\"": | tee -a "${TOPDIR}/files.info"
    ca="${CURLARGS}"
    if [ "${action}" = "PUT" ] ; then
        url="${ARTIFACTORY_URL}/${DEB_REPO}/pool/${COMPONENT}/${deb}${DEB_MATRIX};git_hash=${rippled_git_hash}"
        ca="${ca} -T${deb}"
    elif [ "${action}" = "GET" ] ; then
        url="${ARTIFACTORY_URL}/api/storage/${DEB_REPO}/pool/${COMPONENT}/${deb}"
    fi
    echo "file info request url --> ${url}"
    eval "curl ${ca} \"${url}\"" | jq -M "${filter}" | tee -a "${TOPDIR}/files.info"
done
echo "}," >> "${TOPDIR}/files.info"

# RPM

cd $TOPDIR
cd build/rpm/packages
. ./build_vars

RIPPLED_PKG=$(ls rippled-[0-9]*.x86_64.rpm)
RIPPLED_DBG_PKG=$(ls rippled-debuginfo*.rpm)
echo "\"rpms\": {" >> "${TOPDIR}/files.info"
for rpm in ${RIPPLED_PKG} ${RIPPLED_DBG_PKG}; do
    # first item doesn't get a comma separator
    if [ $rpm != $RIPPLED_PKG ] ; then
        echo "," >> "${TOPDIR}/files.info"
    fi
    echo "\"${rpm}\"": | tee -a "${TOPDIR}/files.info"
    ca="${CURLARGS}"
    if [ "${action}" = "PUT" ] ; then
        url="${ARTIFACTORY_URL}/${RPM_REPO}/${COMPONENT}/${rpm};git_hash=${rippled_git_hash}"
        ca="${ca} -T${rpm}"
    elif [ "${action}" = "GET" ] ; then
        url="${ARTIFACTORY_URL}/api/storage/${RPM_REPO}/${COMPONENT}/${rpm}"
    fi
    echo "file info request url --> ${url}"
    eval "curl ${ca} \"${url}\"" | jq -M "${filter}" | tee -a "${TOPDIR}/files.info"
done
echo "}}" >> "${TOPDIR}/files.info"
jq '.' "${TOPDIR}/files.info" > "${TOPDIR}/files.info.tmp"
mv "${TOPDIR}/files.info.tmp" "${TOPDIR}/files.info"

if [ ! -z "${SLACK_NOTIFY_URL}" ] && [ "${action}" = "GET" ] ; then
    # extract files.info content to variable and sanitize so it can
    # be interpolated into a slack text field below
    finfo=$(cat ${TOPDIR}/files.info | sed -e ':a' -e 'N' -e '$!ba' -e 's/\n/\\n/g' | sed -E 's/"/\\"/g')
    # try posting file info to slack.
    # can add channel field to payload if the
    # default channel is incorrect. Get rid of
    # newlines in payload json since slack doesn't accept them
    CONTENT=$(tr -d '[\n]' <<JSON
       payload={
         "username": "GitlabCI",
         "text": "The package build for branch \`${CI_COMMIT_REF_NAME}\` is complete. File hashes are: \`\`\`${finfo}\`\`\`",
         "icon_emoji": ":package:"}
JSON
)
    curl ${SLACK_NOTIFY_URL} --data-urlencode "${CONTENT}"
fi
