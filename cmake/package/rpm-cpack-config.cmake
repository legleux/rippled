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

#[[ These might be necessary ?
set(CPACK_RPM_USER_FILELIST
    "%config /lib/systemd/system/clio.service"
    "%config /opt/clio/bin/clio_server"
    "%config /opt/clio/etc/config.json"
)


## more hints?
set(CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION
/lib
/lib/systemd
/lib/systemd/system
/opt
)

# #create a directory for the symlinks to be created
set(symlink_directory "${CMAKE_CURRENT_BINARY_DIR}/symlink")
execute_process(COMMAND ${CMAKE_COMMAND} -E make_directory ${symlink_directory})

# #symlink for clio_server
execute_process(COMMAND ${CMAKE_COMMAND}
    -E create_symlink /opt/clio/bin/clio_server ${symlink_directory}/clio_server)
install(FILES ${symlink_directory}/clio_server DESTINATION /usr/local/bin)


set(CPACK_RPM_PACKAGE_RELEASE_DIST "el7")
string(REPLACE "-" ";" VERSION_LIST ${CPACK_PACKAGE_VERSION})
list(GET VERSION_LIST 0 CPACK_RPM_PACKAGE_VERSION)


]]
message("RPM CPACK_PACKAGE_FILE_NAME: ${CPACK_PACKAGE_FILE_NAME}")
