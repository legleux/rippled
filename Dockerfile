FROM debian:12-slim AS base

RUN apt-get update && apt-get install -y curl wget jq
RUN echo 'alias l="ls -l"' >> ~/.bashrc

FROM base AS cmake
RUN <<EOF
    version="3.31.6"
    install_dir="/opt/cmake/${version}"
    script="cmake-${version}-linux-x86_64.sh"
    curl -OJLs "https://github.com/Kitware/CMake/releases/download/v${version}/${script}"
    chmod +x "${script}"
    mkdir -p "${install_dir}"
    "./${script}" --prefix="${install_dir}" --skip-license --exclude-subdir
    CMAKE_ROOT=/opt/cmake/${version}/share/cmake-${version}
    echo "export CMAKE_ROOT=${CMAKE_ROOT%.*}" >> /root/.bashrc
    # ./cmake-3.31.6-linux-x86_64.sh --skip-license
    # curl -OJL https://github.com/Kitware/CMake/releases/download/v4.0.0-rc2/cmake-4.0.0-rc2-linux-x86_64.tar.gz
EOF
# FROM debian:12-slim AS base
# RUN <<EOF
#     apt-get update
#     apt-get install -y build-essential wget
# EOF
# RUN <<EOF
#     VERSION="12.4.0"
#     url="https://gcc.gnu.org/pub/gcc/releases/gcc-${VERSION}/gcc-${VERSION}.tar.gz"
#     # url="https://github.com/gcc-mirror/gcc/archive/refs/tags/releases/gcc-${VERSION}.tar.gz"
#     wget $url
#     src_dir="gcc-${VERSION}"

#     bld_dir="gcc-build"
#     mkdir "${src_dir}"
#     tar xvf "gcc-${VERSION}.tar.gz" -C "${src_dir}" --strip-components=1
#     cd "${src_dir}" && ./contrib/download_prerequisites
# EOF
# # --build=x86_64-linux-gnu

# RUN <<EOF
#     mkdir gcc-build && cd gcc-build
#     ../gcc-12.4.0/configure \
#         --disable-bootstrap \
#         --host=x86_64-linux-gnu \
#         --target=x86_64-linux-gnu \
#         --prefix=/opt/gcc-12.4.0 \
#         --enable-checking=release \
#         --enable-languages=c,c++ \
#         --disable-multilib \
#         --program-suffix=-12.4 \
#         --enable-gold
#     make -j$(nproc)
#     make install-strip
# EOF

# Link all files from  /usr/local/<library>/{bin,lib,include} into /usr/local/{bin,lib,include}

FROM python:3-slim-bookworm AS conan

RUN pip install conan --no-cache-dir --target /opt/conan


# FROM debian:12-slim AS compiler
# FROM python:3-slim-bookworm AS conan_deps
# RUN apt-get update && apt-get install -y git
# COPY --from=ghcr.io/astral-sh/uv:0.6.3 /uv /uvx /bin/
# WORKDIR /root/
# COPY rippled_src rippled

# COPY --from=cmake /opt/ /opt/
# COPY --from=base /root/.bashrc /root/.bashrc
# RUN sed -i 's/ls-l/"ls -l"/g' .bashrc
# RUN ln -s /opt/cmake/3.31.6/bin/cmake /usr/local/bin/cmake
# ENV CMAKE_ROOT=/opt/cmake/3.31.6/share/cmake-3.31
# ENV UV_TOOL_DIR=/root/conan
# RUN uv tool install conan

FROM gcc:12 AS conan_build
COPY --from=cmake /root/.bashrc /root/.bashrc
COPY --from=cmake /opt/ /opt/
WORKDIR /root/
RUN apt-get update && apt-get install -y python3-pip
RUN <<EOF
    set -eux
    pip install --no-cache-dir --break-system-packages conan
EOF
RUN . ~/.bashrc && ln -s $(realpath $CMAKE_ROOT/../../bin/cmake) /usr/local/bin/cmake
ENV CONAN_HOME=/root/conan_home
RUN mkdir -p $CONAN_HOME/profiles

COPY <<EOF /root/conan_home/profiles/default
[settings]
arch=x86_64
build_type=Release
compiler=gcc
compiler.cppstd=20
compiler.libcxx=libstdc++11
compiler.version=12
os=Linux
[options]
&:tests=True
&:xrpld=True
EOF

# RUN git clone --depth 1 --branch develop https://github.com/XRPLF/rippled.git /root/rippled
COPY rippled_src /root/rippled

RUN <<EOF
    mkdir build
    cd build
    conan install ../rippled/ -of . -b missing
EOF
FROM conan_build AS rippled_build
# COPY --from=cmake /root/.bashrc /root/.bashrc
# COPY --from=cmake /opt/ /opt/
WORKDIR /root/
# COPY --from=conan_build /root/build /root/build
# COPY --from=conan_build /root/build /root/rippled

RUN <<EOF
    cd build
    cmake ../rippled \
        -G "Unix Makefiles" \
        -DCMAKE_TOOLCHAIN_FILE=./build/generators/conan_toolchain.cmake \
        -DCMAKE_POLICY_DEFAULT_CMP0091=NEW \
        -DCMAKE_BUILD_TYPE=Release
EOF

RUN cd build && cmake --build . -j 24
RUN cd build && cmake --build . --target install/strip

FROM debian:12-slim AS rippled

COPY --from=rippled_build /usr/local/bin/rippled /usr/local/bin/rippled
VOLUME /var/lib/ripple


# FROM debian:12-slim AS rippled_build
# FROM debian:12-slim AS rippled
# cmake-4.0.0-rc2-linux-x86_64.sh
#


## Maybe to this for the XMake part:
# ARG GCC_VERSION=13
# FROM gcc:$GCC_VERSION

# # renovate: datasource=github-releases depName=Kitware/CMake
# ARG CMAKE_VERSION=3.31.6

# RUN wget https://github.com/Kitware/CMake/releases/download/v${CMAKE_VERSION}/cmake-${CMAKE_VERSION}-Linux-x86_64.sh \
#       -q -O /tmp/cmake-install.sh \
#       && chmod u+x /tmp/cmake-install.sh \
#       && mkdir /usr/bin/cmake \
#       && /tmp/cmake-install.sh --skip-license --prefix=/usr/bin/cmake \
#       && rm /tmp/cmake-install.sh

# ENV PATH="/usr/bin/cmake/bin:${PATH}"
