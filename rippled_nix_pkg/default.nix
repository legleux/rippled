{
  lib,
  stdenv,
  fetchgit,
  fetchpatch,
  git,
  cmake,
  pkg-config,
  openssl,
  boost182,
  grpc,
  protobuf
}:

let
  sqlite3 = fetchgit rec {
    url = "https://github.com/sqlite/sqlite.git";
    rev = "3.42.0";
    sha256 = "0h971364bhpdqf0hmab4bpqaalaildrl70nd51n6yjb3f2samnl1";
  };

  # boostSharedStatic = boost.override {
  #   enableShared = true;
  #   enabledStatic = true;
  # };

  nudb = fetchgit rec {
    url = "https://github.com/CPPAlliance/NuDB.git";
    rev = "2.0.8";
    sha256 = "0bgr92zi97vjkgsz3miyxr03byg3p4zg8f023x38f9n3lhc13mhd";
    leaveDotGit = true;
    fetchSubmodules = true;
    postFetch = "cd $out && git tag ${rev}";
  };

  lz4 = fetchgit rec {
    url = "https://github.com/lz4/lz4.git";
    rev = "v1.9.3";
    sha256 = "1w02kazh1fps3sji2sn89fz862j1199c5ajrqcgl1bnlxj09kcbz";
    leaveDotGit = true;
    fetchSubmodules = false;
    postFetch = "cd $out && git tag ${rev}";
  };

  libarchive = fetchgit rec {
    url = "https://github.com/libarchive/libarchive.git";
    rev = "v3.6.2";
    sha256 = "06632mayzjhrmd91sg8sjw3484dd4fwj9jwflmkwl7spz7mc01n1";
    leaveDotGit = true;
    fetchSubmodules = false;
    postFetch = "cd $out && git tag ${rev}";
  };

  soci = fetchgit {
    url = "https://github.com/SOCI/soci.git";
    rev = "4.0.3";
    sha256 = "12aq7pama96l2c1kmfkclb4bvrsxs9a8ppgk5gmzw45w2lg35i0y";
    leaveDotGit = true;
    fetchSubmodules = false;
  };

  # google-test = fetchgit {
  #   url = "https://github.com/google/googletest.git";
  #   rev = "5ec7f0c4a113e2f18ac2c6cc7df51ad6afc24081";
  #   sha256 = "1ch7hq16z20ddhpc08slp9bny29j88x9vr6bi9r4yf5m77xbplja";
  #   leaveDotGit = true;
  #   fetchSubmodules = false;
  # };

  # google-benchmark = fetchgit {
  #   url = "https://github.com/google/benchmark.git";
  #   rev = "5b7683f49e1e9223cf9927b24f6fd3d6bd82e3f8";
  #   sha256 = "0kcmb83framkncc50h0lyyz7v8nys6g19ja0h2p8x4sfafnnm6ig";
  #   leaveDotGit = true;
  #   fetchSubmodules = false;
  # };

  date = fetchgit {
    url = "https://github.com/HowardHinnant/date.git";
    rev = "3.0.1";
    sha256 = "1qk7pgnk0bpinja28104qha6f7r1xwh5dy3gra7vjkqwl0jdwa35";
    leaveDotGit = true;
    fetchSubmodules = false;
  };
in stdenv.mkDerivation rec {
  pname = "rippled";
  version = "nix"; # "2.2.0-b1";

  src = fetchgit {
    url = "https://github.com/legleux/rippled.git";
    rev = version;
    leaveDotGit = true;
    # fetchSubmodules = true;
    sha256 = "0307v70npxpwv9rvlxg92dfd561f83znp52zfr7mk3wdwq9za2j8";
  };

  patches = [
    # Fix gcc-13 build due to missing <cstdint> includes:
    #   https://github.com/XRPLF/rippled/pull/4555
    (fetchpatch {
      name = "gcc-13.patch";
      url  = "https://github.com/XRPLF/rippled/commit/c9a586c2437bc8ffd22e946c82e1cbe906e1fc40.patch";
      hash = "sha256-+4BDTMFoQWUHljgwGB1gtczVPQH/U5MA0ojbnBykceg=";
      excludes = [ "src/ripple/basics/StringUtilities.h" ];
    })
  ];

  hardeningDisable = ["format"];
  cmakeFlags = ["-Dstatic=OFF" "-Dtests=OFF" "-DBoost_NO_BOOST_CMAKE=ON" "-Drocksdb=OFF" ];

  nativeBuildInputs = [ pkg-config cmake git ];
  # buildInputs = [ openssl openssl.dev boostSharedStatic grpc protobuf libnsl snappy ];
  buildInputs = [ openssl openssl.dev boost182 grpc protobuf ];

  preConfigure = ''
    export HOME=$PWD

    git config --global protocol.file.allow always
    git config --global url."file://${lz4}".insteadOf "${lz4.url}"
    git config --global url."file://${libarchive}".insteadOf "${libarchive.url}"
    git config --global url."file://${soci}".insteadOf "${soci.url}"
    git config --global url."file://${nudb}".insteadOf "${nudb.url}"
    git config --global url."file://${date}".insteadOf "${date.url}"
  '';

  doCheck = true;
  checkPhase = ''
    ./rippled --unittest
  '';

  meta = with lib; {
    description = "Ripple P2P payment network reference server";
    homepage = "https://github.com/XRPLF/rippled";
    maintainers = with maintainers; [ offline legleux ];
    license = licenses.isc;
    platforms = platforms.linux;
    mainProgram = "rippled";
  };
}
