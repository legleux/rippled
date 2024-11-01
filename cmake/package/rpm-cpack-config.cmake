set(CPACK_RPM_FILE_NAME RPM-DEFAULT)

if(NOT DEFINED CPACK_RPM_PACKAGE_RELEASE)
    set(CPACK_RPM_PACKAGE_RELEASE ${PKG_REL_VERSION}) # ? the "N" <pkg_name>-<ver>-N.amd64.rpm
endif()

set(CPACK_RPM_PACKAGE_GROUP server) # TODO: Consult the docs for what goes here
set(CPACK_RPM_PACKAGE_SOURCES OFF) # TODO: Confirm this does something
set(CPACK_RPM_PACKAGE_URL "<set_to_rpm_repo>") # FIXME: set to real repo
set(CPACK_RPM_PACKAGE_LICENSE ISC)

find_program(DPKG dpkg) # Just so we have both pkgs as "amd64"
if(DPKG)
    execute_process(COMMAND ${DPKG} --print-architecture
        OUTPUT_VARIABLE CPACK_RPM_PACKAGE_ARCHITECTURE
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
endif()
