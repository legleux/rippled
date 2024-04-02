#!/usr/bin/env bash

function error {
    echo $1
    exit 1
}

cd ..
export RIPPLED_VERSION=$(egrep -i -o "\b(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(-[0-9a-z\-]+(\.[0-9a-z\-]+)*)?(\+[0-9a-z\-]+(\.[0-9a-z\-]+)*)?\b" src/ripple/protocol/impl/BuildInfo.cpp)

: ${PKG_OUTDIR:=pkgs}
export PKG_OUTDIR
