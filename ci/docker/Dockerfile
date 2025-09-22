ARG DISTRO=ubuntu
ARG VERSION=jammy
ARG COMPILER=gcc
ARG COMPILER_VERSION=12
ARG BUILD_TYPE=Release
ARG TESTS=True

FROM ghcr.io/xrplf/ci/${DISTRO}-${VERSION}:${COMPILER}-${COMPILER_VERSION} AS base

ENV CONAN_HOME=/root/.conan2
SHELL ["/bin/bash", "-c"]
WORKDIR /root

FROM base AS build
ARG BUILD_TYPE
ARG CONAN_REMOTE="xrplf https://conan.ripplex.io"
ARG TESTS

COPY . repo
RUN <<EOF
    set -ex
    build_dir=${build_dir:-build}
    num_proc=$(nproc --ignore 2)
    if [ "$num_proc" -lt 3 ]; then
        echo "Building with 1 processor."
        num_proc=1
    fi

    echo "core.download:parallel=$(nproc)" >> "${CONAN_HOME}/global.conf"
    echo "tools.build:jobs=${num_proc}" >>  "${CONAN_HOME}/global.conf"

    conan remote add --index 0 ${CONAN_REMOTE}
    sed -i 's/^\(compiler\.cppstd=\)gnu17$/\120/' "${CONAN_HOME}/profiles/default"

    conan build repo \
        --options:h "&:xrpld=True" \
        --options:h "&:tests=${TESTS}" \
        --output-folder "${build_dir}"
        # \
        # --build="soci/4.0.3" \
        # --build="b2/5.3.3" \
        # --build="boost/1.88.0"

    # rippled=$(find ${build_dir} -name rippled -type f)
    cp "${build_dir}/build/${BUILD_TYPE}/rippled" .
    strip rippled
EOF

FROM debian:bullseye-slim AS rippled
ARG BUILD_TYPE
ARG TARGETARCH
SHELL ["/bin/bash", "-c"]

RUN <<EOF
    pkgs+=(ca-certificates)  # Warning in logs about SSL if not installed
    pkgs+=(curl)             # For healthcheck
    pkgs+=(jq)               # For healthcheck
    pkgs+=(vim-tiny)         # For ... vim
    apt-get update && apt-get install -y "${pkgs[@]}"
    rm -rf /var/lib/apt/lists/* && apt-get clean
EOF

COPY --from=build /root/rippled /opt/ripple/bin/rippled
COPY --from=build /root/repo/cfg/rippled-example.cfg /opt/ripple/etc/rippled.cfg
COPY --from=build /root/repo/cfg/validators-example.txt /opt/ripple/etc/validators.txt

RUN ln -s /opt/ripple/bin/rippled /usr/local/bin/rippled
RUN mkdir -p /etc/opt && ln -s /opt/ripple/etc/ /etc/opt/ripple

RUN <<EOF
if [ "$TARGETARCH" = "arm64" ]; then \
      apt-get update && \
      apt-get install --yes libatomic1 && \
      rm -rf /var/lib/apt/lists/* && \
      apt-get clean; \
    fi
EOF

ENTRYPOINT ["/opt/ripple/bin/rippled"]
