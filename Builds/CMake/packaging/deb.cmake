set(CPACK_GENERATOR "DEB")
set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "http://github.com/XRPLF/clio")
set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "rippled")
set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "amd64") # On debian it's always been "amd64"

set(CPACK_DEBIAN_DEBUGINFO_PACKAGE ON)

set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
    ${CMAKE_SOURCE_DIR}/Builds/CMake/packaging/deb/rippled.preinst;
    ${CMAKE_SOURCE_DIR}/Builds/CMake/packaging/deb/rippled.postinst;
    ${CMAKE_SOURCE_DIR}/Builds/CMake/packaging/deb/rippled.prerm;
    ${CMAKE_SOURCE_DIR}/Builds/CMake/packaging/deb/rippled.postrm
)
