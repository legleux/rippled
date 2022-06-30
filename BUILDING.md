## Platforms

We do not recommend Windows for rippled production use at this time. Currently,
the Ubuntu platform has received the highest level of quality assurance,
testing, and support. Additionally, 32-bit Windows versions are not supported.


## Prerequisites

To build this package, you will need to install [Conan][] and [CMake][] (>=
3.16).

[Conan]: https://conan.io/downloads.html
[CMake]: https://cmake.org/download/

Linux developers will commonly have a default [Conan profile][1] that compiles
with GCC and links with libstdc++.
You can check your default profile with `conan profile show default`.
(If you have not yet generated a default profile, then you can with `conan
profile new default --detect`.)

If you are linking with libstdc++ (see profile setting `compiler.libcxx`),
then you will need to choose a Conan profile with the `libstdc++11` ABI.
The easiest way is to change your default profile to use it like this:

```
conan profile update settings.compiler.libcxx=libstdc++11 default
```

Windows developers will commonly have a default Conan profile that is missing
a selection for the [runtime library][2].
We recommend that you make that selection in your Conan profile to ensure
a consistent choice among all packages.

```
conan profile update settings.compiler.runtime=MT default
```

We find it necessary to use the x64 native build tools on Windows.
An easy way to do that is to run the shortcut "x64 Native Tools Command
Prompt" for the version of Visual Studio that you have installed.

Windows developers must build rippled and thus its dependencies for the x64
architecture.
That can be guaranteed with a Conan profile setting:

```
conan profile update settings.compiler.arch=x86_64 default
```


## Branches

For a stable release, choose the `master` branch or one of the tagged releases
listed on [rippled's GitHub page](https://github.com/ripple/rippled/releases).

```
git checkout master
```

To test the latest release candidate, choose the `release` branch.

```
git checkout release
```

If you are contributing or want the latest set of untested features,
then use the `develop` branch instead.

```
git checkout develop
```


## How to build

Here is an example building the package in a release configuration.
These commands should work in both Bash and PowerShell.
(Replace "Release" with "Debug" if you want to build that configuration
instead.)

```
conan export external/rocksdb
# The build directory must be named "build".
mkdir build
cd build
conan install .. --build missing --settings build_type=Release
cmake --toolchain=generators/conan_toolchain.cmake ..
cmake --build . --config Release
./rippled --unittest
```

In addition to choosing a different configuration, there are a few options you
can pass to the CMake configure command.
In particular, the `unity` option allows you to select between the unity and
non-unity builds. Unity builds may be faster to compile (at the cost of much
more memory) since they combine multiple sources into a single compiliation
unit - this is the default if you don't specify. Non-unity builds can be
helpful for detecting `#include` omissions or for finding other build-related
issues, but aren't generally needed for testing and running.

- `-Dunity=ON` to enable/disable unity builds (defaults to ON)
- `-Dassert=ON` to enable asserts
- `-Djemalloc=ON` to enable jemalloc support for heap checking
- `-Dsan=thread` to enable the thread sanitizer with clang
- `-Dsan=address` to enable the address sanitizer with clang
- `-Dreporting=ON` to build code necessary for reporting mode (defaults to OFF)


## How to add a dependency

If you want to experiment with a new package, here are the steps to get it
working:

1. Search for the package on [Conan Center](https://conan.io/center/).
1. In [`conanfile.py`](./conanfile.py):
    1. Add a version of the package to the `requires` property.
    1. Change any default options for the package by adding them to the
    `default_options` property (with syntax `'$package:$option': $value`)
1. In [`CMakeLists.txt`](./CMakeLists.txt):
    1. Add a call to `find_package($package REQUIRED)`.
    1. Link a library from the package to the target `ripple_libs` (search for
    the existing call to `target_link_libraries(ripple_libs INTERFACE ...)`).
1. Start coding! Don't forget to include whatever headers you need from the
   package.


[1]: https://docs.conan.io/en/latest/reference/profiles.html
[2]: https://docs.microsoft.com/en-us/cpp/build/reference/md-mt-ld-use-run-time-library
[3]: https://cmake.org/cmake/help/git-stage/variable/CMAKE_MSVC_RUNTIME_LIBRARY.html
