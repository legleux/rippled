#!/usr/bin/env bash

conan profile new --detect default
conan profile update settings.compiler.libcxx=libstdc++11 default
conan profile update settings.compiler.cppstd=20 default
conan profile update options.rocksdb=False default
conan profile update options.tests=False default