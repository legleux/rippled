# CPACK_DEBIAN_PACKAGE_NAME
# CPACK_DEBIAN_PACKAGE_ARCHITECTURE # Should not need to be set?
# CPACK_DEBIAN_PACKAGE_DEPENDS # TODO: Set/generate this
# TODO: What section does a server go in?
# net https://packages.debian.org/stable/net/
# libs https://packages.debian.org/stable/libs/
set(CPACK_BINARY_DEB ON)
set(CPACK_SOURCE_DEB OFF)

set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)

# Create a package with debug symbols
set(CPACK_DEBIAN_DEBUGINFO_PACKAGE ON)

if(NOT DEFINED CPACK_DEBIAN_PACKAGE_RELEASE)
    set(CPACK_DEBIAN_PACKAGE_RELEASE ${PKG_REL_VERSION}) # ? the "N" in <pkg_name>_<ver>-N_amd64.deb
endif()
# set(CPACK_DEBIAN_PACKAGE_RELEASE ${PKG_RELEASE_VERSION}) # ? the 1 in pkg_N-1.deb
## if ^ isn't set, is not set then hyphens are not allowe in
# set(CPACK_DEBIAN_PACKAGE_VERSION 5)
# set(CPACK_DEBIAN_PACKAGE_EPOCH 6) # ?

set(CPACK_DEBIAN_PACKAGE_SECTION "net;libs")

# CPACK_DEBIAN_PACKAGE_PRIORITY # defaults to "optional"

## These need some tools
# set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)  # set?
# set(CPACK_DEBIAN_PACKAGE_GENERATE_SHLIBS ON) # set ?

#[[ # TODO: Test if these are needed
set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
  ${CMAKE_SOURCE_DIR}/cmake/package/deb/conffiles
  ${CMAKE_SOURCE_DIR}/cmake/package/deb/postinst
)
]]
# set(CPACK_PACKAGE_VERSION ${VERSION})
message("CPACK_PACKAGE_VERSION: ${CPACK_PACKAGE_VERSION}")

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
# set(CPACK_PACKAGE_FILE_NAME "${CMAKE_PROJECT_NAME}_${CPACK_DEBIAN_PACKAGE_VERSION}-${CPACK_DEBIAN_PACKAGE_RELEASE}_${CPACK_SYSTEM_NAME}")
# message("DEB CPACK_PACKAGE_FILE_NAME: ${CPACK_PACKAGE_FILE_NAME}")
