set(CPACK_BINARY_DEB ON)
set(CPACK_SOURCE_DEB OFF)

set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)

# Create a package with debug symbols
set(CPACK_DEBIAN_DEBUGINFO_PACKAGE ON)

if(NOT DEFINED CPACK_DEBIAN_PACKAGE_RELEASE)
    set(CPACK_DEBIAN_PACKAGE_RELEASE ${PKG_REL_VERSION}) # ? the "N" in <pkg_name>_<ver>-N_amd64.deb
endif()

set(CPACK_DEBIAN_PACKAGE_SECTION "net;libs")

# CPACK_DEBIAN_PACKAGE_PRIORITY # defaults to "optional"

## These need some tools
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_PACKAGE_GENERATE_SHLIBS ON)

set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
  ${CMAKE_SOURCE_DIR}/cmake/package/deb/conffiles
  ${CMAKE_SOURCE_DIR}/cmake/package/deb/postinst
)

string(REPLACE "-" ";" VERSION_LIST ${CPACK_PACKAGE_VERSION})
message("VERSION_LIST: ${VERSION_LIST}")

list(GET VERSION_LIST 0 CPACK_PACKAGE_VERSION)
list (LENGTH VERSION_LIST _len)
if (${_len} GREATER 1)
    list(GET VERSION_LIST 1 PRE_VERSION)
    message("PRE_VERSION: ${PRE_VERSION}")
    set(CPACK_DEBIAN_PACKAGE_VERSION "${CPACK_PACKAGE_VERSION}~${PRE_VERSION}")
else()
    set(CPACK_DEBIAN_PACKAGE_VERSION "${CPACK_PACKAGE_VERSION}")
endif()
