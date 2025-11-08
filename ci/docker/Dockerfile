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
ARG data

RUN mkdir -p /opt/ripple/{bin,etc}
RUN echo "config=${data}" >> /opt/ripple/etc/rippled.cfg
COPY <<"EOF" /opt/ripple/bin/rippled
#!/usr/bin/env bash
. /opt/ripple/etc/rippled.cfg
echo "am rippled! With config! $config"
EOF
RUN chmod +x /opt/ripple/bin/rippled


FROM ${DISTRO}:${VERSION} AS rippled
ARG TARGETARCH
SHELL ["/bin/bash", "-c"]

RUN <<EOF
    pkgs+=(ca-certificates) # Warning in logs about SSL if not installed
    pkgs+=(curl)            # For healthcheck simplicity
    pkgs+=(jq)              # For healthcheck simplicity
    pkgs+=(vim)             # For... vim-ing
    if [ "$TARGETARCH" = "arm64" ]; then
        pkgs+=(libatomic1)
    fi
    apt-get update && apt-get install -y "${pkgs[@]}"
    rm -rf /var/lib/apt/lists/* && apt-get clean
EOF

COPY --from=build /opt/ripple/bin/ /opt/ripple/bin/
COPY --from=build /opt/ripple/etc/ /opt/ripple/etc/

RUN ln -s /opt/ripple/bin/rippled /usr/local/bin/rippled
RUN mkdir -p /etc/opt && ln -s /opt/ripple/etc/ /etc/opt/ripple

ENTRYPOINT ["/opt/ripple/bin/rippled"]
