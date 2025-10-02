# Testing the rippled install
## Build it

### Configure it

```
cmake \
  -S . \
  -B build \
  -D xrpld=ON \
  -D use_mold=ON \
  -Dvalidator_keys=ON \
  -DCMAKE_TOOLCHAIN_FILE=./rippled_deps/build/generators/conan_toolchain.cmake \
  -DCMAKE_C_COMPILER_LAUNCHER=ccache \
  -DCMAKE_CXX_COMPILER_LAUNCHER=ccache
```

### Build it

```
cmake \
  --build build \
  --target rippled \
  --target validator-keys \
  --parallel $(nproc --ignore 2)
```
### Test it

```shell
ctest build
```

### Install it
```shell
DESTDIR=$PWD/my_install_dir cmake --install build --strip
```
## Changes

### Before

- CMake warning

    CMake Warning (dev) at /usr/share/cmake/Modules/FetchContent.cmake:1953 (message):
      Calling FetchContent_Populate(validator_keys_src) is deprecated, call
      FetchContent_MakeAvailable(validator_keys_src) instead.  Policy CMP0169 can
      be set to OLD to allow FetchContent_Populate(validator_keys_src) to be
      called directly for now, but the ability to call it with declared details
      will be removed completely in a future version.
    Call Stack (most recent call first):
      cmake/RippledValidatorKeys.cmake:19 (FetchContent_Populate)
      CMakeLists.txt:151 (include)
    This warning is for project developers.  Use -Wno-dev to suppress it.

- Redundant `_src` suffix (CMake labels source nicely)

  tree -L 1 build/_deps
  build/_deps
  ├── validator_keys_src-build
  ├── validator_keys_src-src
  └── validator_keys_src-subbuild


### After:










######
Cache directory:      /home/emel/.cache/ccache
Config file:          /home/emel/.config/ccache/ccache.conf
System config file:   /etc/ccache.conf
Stats updated:        Thu Oct  2 10:57:40 2025
Cacheable calls:      2348 / 2356 (99.66%)
  Hits:                155 / 2348 ( 6.60%)
    Direct:            130 /  155 (83.87%)
    Preprocessed:       25 /  155 (16.13%)
  Misses:             2193 / 2348 (93.40%)
Uncacheable calls:       8 / 2356 ( 0.34%)
  Called for linking:    8 /    8 (100.0%)
Successful lookups:
  Direct:              130 / 2348 ( 5.54%)
  Preprocessed:         25 / 2218 ( 1.13%)
Local storage:
  Cache size (GiB):    1.7 /  5.0 (34.23%)
  Files:              4411
  Hits:                155 / 2348 ( 6.60%)
  Misses:             2193 / 2348 (93.40%)
  Reads:              4696
  Writes:             4411
