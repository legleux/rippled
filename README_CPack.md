# Build `rippled` Binary Packages using CPack

1. What is the Docker image to use?
   1. What is the tag?
   2. How is it updated?
   3. For DEB?
   4. For RPM?
   
2. What is the command to run?

3. How does this run in CI?


# to do

1. Add "build packages" script or CMake target
2. 


Don't package all installed headers?


# Installation of `rippled` with CPack

## The `rippled` `install` target

Some configuration variables

    src_dir=$PWD
    bin_dir=$src_dir/build/cmake/debug/
    install_dir="rippled_install"
    

Also supported

    --prefix <install_dir> # Don't install to the default /usr
    --config <cmake_config>
    --component <what is this?>
    --strip # use this for release install? but we don't strip


Example using configuration from above

`cmake --install $bin_dir --prefix $install_dir`

    $ tree -L 3 $install_dir
    rippled_install
    ├── bin
    │   ├── rippled
    │   └── xrpld -> rippled
    ├── etc
    │   ├── rippled.cfg
    │   └── validators.txt
    ├── include
    │   ├── ed25519.h
    │   ├── ripple -> xrpl
    │   ├── secp256k1_ecdh.h
    │   ├── secp256k1_extrakeys.h
    │   ├── secp256k1.h
    │   ├── secp256k1_preallocated.h
    │   ├── secp256k1_schnorrsig.h
    │   └── xrpl
    │       ├── basics
    │       ├── beast
    │       ├── crypto
    │       ├── json
    │       ├── proto
    │       ├── protocol
    │       ├── resource
    │       └── server
    └── lib
        ├── cmake
        │   ├── ed25519
        │   ├── libsecp256k1
        │   └── ripple
        ├── libed25519.a
        ├── libsecp256k1.a
        ├── libxrpl.a
        └── libxrpl.libpb.a
