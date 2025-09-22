#!/usr/bin/env sh
set -ex
pkgtype=$1
if [ "${pkgtype}" = "rpm" ] ; then
    container_name="${RPM_CONTAINER_NAME}"
elif [ "${pkgtype}" = "dpkg" ] ; then
    container_name="${DPKG_CONTAINER_NAME}"
else
    echo "invalid package type"
    exit 1
fi

if docker pull "${ARTIFACTORY_HUB}/${container_name}:latest_${CI_COMMIT_REF_SLUG}"; then
    echo "found container for latest - using as cache."
    docker tag \
       "${ARTIFACTORY_HUB}/${container_name}:latest_${CI_COMMIT_REF_SLUG}" \
       "${container_name}:latest_${CI_COMMIT_REF_SLUG}"
    args=(--cache-from "${container_name}:latest_${CI_COMMIT_REF_SLUG}")
fi

commit_hash=$(git log --pretty=%H -1)
container_label=${commit_hash}

if [ "${pkgtype}" = "dpkg" ] ; then
    args+=(--build-arg DIST_TAG=18.04)
fi

if [ "${pkgtype}" = "rpm" ] ; then
    docker build \
        --pull \
        --build-arg GIT_COMMIT=${commit_hash} \
        -t rippleci/rippled-${pkgtype}-builder:${container_label} \
        "${args[@]}" \
        -f centos-builder/Dockerfile .
elif [ "${pkgtype}" = "dpkg" ] ; then
    docker build \
        --pull \
        --build-arg GIT_COMMIT=${commit_hash} \
        -t rippled-${pkgtype}-builder:${container_label} \
        "${args[@]}" \
        -f ubuntu-builder/Dockerfile .
fi
