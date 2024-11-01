# CPACK_DEBIAN_PACKAGE_NAME
# CPACK_DEBIAN_PACKAGE_ARCHITECTURE # Should not need to be set?
# CPACK_DEBIAN_PACKAGE_DEPENDS # TODO: Set/generate this
# TODO: What section does a server go in?
# net https://packages.debian.org/stable/net/
# libs https://packages.debian.org/stable/libs/
set(CPACK_BINARY_DEB ON)
set(CPACK_SOURCE_DEB OFF)
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)

if(NOT DEFINED CPACK_DEBIAN_PACKAGE_RELEASE)
    set(CPACK_DEBIAN_PACKAGE_RELEASE ${PKG_REL_VERSION}) # ? the "N" in <pkg_name>_<ver>-N_amd64.deb
endif()
# set(CPACK_DEBIAN_PACKAGE_RELEASE ${PKG_RELEASE_VERSION}) # ? the 1 in pkg_N-1.deb
## if ^ isn't set, is not set then hyphens are not allowe in
# set(CPACK_DEBIAN_PACKAGE_VERSION 5)
# set(CPACK_DEBIAN_PACKAGE_EPOCH 6) # ?

message(DEBUG "******************************************************")
message(DEBUG "*************ooooooooooooooooooooooo******************")
message(DEBUG "*************Building debian package******************")
message(DEBUG "*************ooooooooooooooooooooooo******************")
message(DEBUG "******************************************************")
# set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_PACKAGE_SECTION "net;libs")

# CPACK_DEBIAN_PACKAGE_PRIORITY # defaults to "optional"

## These need some tools
# set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)  # set?
# set(CPACK_DEBIAN_PACKAGE_GENERATE_SHLIBS ON) # set ?
