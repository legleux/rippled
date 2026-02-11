ARG DISTRO=ubuntu
ARG VERSION=jammy
ARG COMPILER=gcc
ARG COMPILER_VERSION=12
ARG BUILD_TYPE=Release

FROM ghcr.io/xrplf/ci/${DISTRO}-${VERSION}:${COMPILER}-${COMPILER_VERSION} AS base
ENV CONAN_HOME=/root/.conan2
SHELL ["/bin/bash", "-c"]
WORKDIR /root

FROM base AS builder
ARG BUILD_TYPE
ARG CONAN_REMOTE="xrplf https://conan.ripplex.io"

# Layer 1: Dependency files only (change infrequently)
COPY conanfile.py                       /root/repo/conanfile.py
COPY CMakeLists.txt                     /root/repo/CMakeLists.txt
COPY conan/                             /root/repo/conan/
COPY cmake/                             /root/repo/cmake/
COPY src/libxrpl/protocol/BuildInfo.cpp /root/repo/src/libxrpl/protocol/BuildInfo.cpp

RUN <<EOF
    set -ex
    cat repo/conan/global.conf > $(conan config home)/global.conf
    conan remote add --force --index 0 ${CONAN_REMOTE}
    conan config install repo/conan/profiles/default --target-folder $(conan config home)/profiles/
    if [ "$TARGETARCH" = "arm64" ]; then
        pkgs+=(libatomic1)
        conan_build="*"
    else
        conan_build="missing"
    fi
    conan install repo \
        --build="${conan_build}" \
        --build:build="${conan_build}" \
        --settings:all build_type="$BUILD_TYPE" \
        --options:host "&:xrpld=True" \
        --options:host "&:tests=False" \
        --output-folder build
EOF

# Layer 2: Full source (changes frequently)
# Build context must be the repo root. Use .dockerignore to exclude build artifacts.
COPY . /root/repo/

RUN <<EOF
    cmake \
        -S repo \
        -B build \
        -G Ninja \
        -Dvalidator_keys=OFF \
        -Dxrpld=ON \
        -Dtests=OFF \
        -Duse_mold=ON \
        -DCMAKE_VERBOSE_MAKEFILE=ON \
        -DCMAKE_TOOLCHAIN_FILE=./build/generators/conan_toolchain.cmake \
        -DCMAKE_BUILD_TYPE=$BUILD_TYPE
EOF
RUN N=$(nproc --ignore 2); cmake --build build --parallel $N
RUN cmake --install build/ --prefix /opt/xrpld --strip

FROM ${DISTRO}:${VERSION} AS xrpld
ARG TARGETARCH
SHELL ["/bin/bash", "-c"]

RUN <<EOF
    pkgs+=(ca-certificates)      # Warning in logs about SSL if not installed
    pkgs+=(curl)                 # For healthcheck simplification
    pkgs+=(jq)                   # For healthcheck simplification
    pkgs+=(vim-tiny)             # For... vim-ing
    if [ "$TARGETARCH" = "arm64" ]; then
        pkgs+=(libatomic1)
    fi
    apt-get update && apt-get install -y "${pkgs[@]}"
    rm -rf /var/lib/apt/lists/* && apt-get clean
EOF

COPY --from=builder /opt/xrpld/bin/ /opt/xrpld/bin/
COPY --from=builder /opt/xrpld/etc/ /opt/xrpld/etc/

RUN mkdir -p /etc/opt && \
    ln -s /opt/xrpld/bin/xrpld /usr/bin/xrpld && \
    ln -s /opt/xrpld/etc/ /etc/opt/xrpld && \
    ## Compatibility with rippled (TODO: Remove when deprecation date reached)
    ln -s /opt/xrpld/bin/xrpld /usr/bin/rippled && \
    ln -s /opt/xrpld/etc/ /etc/opt/ripple

EXPOSE 2459 5005 6006 51235

WORKDIR /opt/xrpld/

ENTRYPOINT ["/opt/xrpld/bin/xrpld"]
