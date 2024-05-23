#!/usr/bin/env bash

set -ex

source_dir=${source_dir:-$PWD}
build_dir=${build_dir:-build}

conan install ${source_dir} \
    --output-folder build \
    --build missing \
    --settings build_type=Release
# conan profile update conf.tools.build:compiler_executables="{'c': '/usr/local/bin/gcc', 'cpp': '/usr/local/bin/g++'}" default
# conan profile update conf.tools.build:compiler_executables="{'c': '/usr/local/bin/gcc', 'cpp': '/usr/local/bin/g++'}" default
# conan profile update env.CC="/usr/local/bin/gcc" default
# conan profile update env.CXX="/usr/local/bin/g++" default

cmake -S ${source_dir} -B build \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE=build/generators/conan_toolchain.cmake

cmake --build build --parallel $(nproc)

cmake --install build --prefix /opt/ripple --strip
