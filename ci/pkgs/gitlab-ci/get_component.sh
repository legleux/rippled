#!/usr/bin/env sh

case ${rippled_branch} in
    develop)
        export COMPONENT="nightly"
        ;;
    release)
        export COMPONENT="unstable"
        ;;
    master)
        export COMPONENT="stable"
        ;;
    *)
        export COMPONENT="_unknown_"
        ;;
esac

if [ -n "${private_component}" ]; then
    export COMPONENT="${private_component}"
fi
