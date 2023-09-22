# cmake_minimum_required(VERSION 3.16)

# project(rippled-pkg)

include(GNUInstallDirs)
# include(FetchContent)
set(CPACK_PACKAGE_NAME ${PROJECT_NAME})
# add_custom_target(rippled
#     COMMAND docker run --rm -it -w /rippled-packages -v ${CMAKE_CURRENT_SOURCE_DIR}:/rippled-packages centos-builder-1.12.0 bash /rippled-packages/build_rippled.sh
#     # COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/docker_build.sh ${DOCKER_SOURCE_DIR}
#     USES_TERMINAL
# )
set(CPACK_PACKAGE_VENDOR "Ripple")
# set(CPACK_PACKAGE_VERSION 0.1.11)

set(CPACK_PACKAGING_INSTALL_PREFIX "/opt/ripple")
# set(CPACK_BUILD_SOURCE_DIRS ${CMAKE_SOURCE_DIR}/src) # maybe?

# maybe rippled should be install(PROGRAMS) for +xpermissions?

#[===================================================================[
    Package rippled
    #]===================================================================]
# install(
#     FILES ${CMAKE_CURRENT_SOURCE_DIR}/rippled_binary/rippled
#     TYPE BIN
#     PERMISSIONS OWNER_READ OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
# )
## or this ↓?
# add_executable(rippled IMPORTED)
# set_property(TARGET rippled PROPERTY IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/build_packages/rippled)
# install(IMPORTED_RUNTIME_ARTIFACTS rippled)

#[===================================================================[
    Package config files
    #]===================================================================]
install(FILES ${CMAKE_SOURCE_DIR}/cfg/rippled-example.cfg DESTINATION ${CMAKE_INSTALL_SYSCONFDIR} RENAME rippled.cfg)
install(FILES ${CMAKE_SOURCE_DIR}/cfg/validators-example.txt DESTINATION ${CMAKE_INSTALL_SYSCONFDIR} RENAME validators.txt)

#[===================================================================[ Package rippled systemd service file, update and log rotation scripts
    #]===================================================================]
    install(FILES ${CMAKE_SOURCE_DIR}/Builds/CMake/packaging/shared/rippled-logrotate DESTINATION /etc/logrotate.d/ RENAME rippled)
    install(FILES ${CMAKE_SOURCE_DIR}/Builds/CMake/packaging/shared/rippled.service DESTINATION /lib/systemd/system/)
    install(FILES ${CMAKE_SOURCE_DIR}/Builds/CMake/packaging/shared/update-rippled-cron DESTINATION /etc)
    install(FILES ${CMAKE_SOURCE_DIR}/Builds/CMake/packaging/shared/update-rippled.sh TYPE BIN)
    install(FILES ${CMAKE_SOURCE_DIR}/bin/getRippledInfo TYPE BIN)

# add_custom_command(build_package ALL ./build_pkg.sh)

## TODO: this needs to be run in docker
# foreach(GENERATOR DEB RPM)
#     # add_custom_target(
#         # ${GENERATOR} cpack -V --debug -G ${GENERATOR} --config ${CMAKE_BINARY_DIR}/CPackConfig.cmake
#         # USES_TERMINAL
#     # )
#     # # TODO: variable for docker container build dir and source dir "/rippled-packages"
#     add_custom_target(${GENERATOR} docker run --rm -it
#         -w /rippled-packages
#         -v ${CMAKE_SOURCE_DIR}:/rippled-packages centos-builder-1.12.0 bash
#         /rippled-packages/build_rippled_packages.sh ${GENERATOR} ${CMAKE_SOURCE_DIR}
#         )
#     #     add_custom_target(${GENERATOR} ${CMAKE_SOURCE_DIR}/build_rippled_packages.sh ${GENERATOR} ${CMAKE_SOURCE_DIR}
#     # )
# endforeach()

# # this target is run inside the container
# add_custom_target(DEB_pkg
#     cpack -V --debug -G DEB --config ${CMAKE_BINARY_DIR}/CPackConfig.cmake
#     )

# add_custom_target(RPM_pkg
#     cpack -V --debug -G RPM --config ${CMAKE_BINARY_DIR}/CPackConfig.cmake
#     )



set(CPACK_OUTPUT_FILE_PREFIX packages)
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "rippled daemon")
set(CPACK_PACKAGE_VERSION "${rippled_version}")
set(CPACK_PACKAGE_VENDOR "Ripple")
set(CPACK_PACKAGE_CONTACT "Ripple Labs Inc. <support@ripple.com>")
set(CPACK_PACKAGE_HOMEPAGE_URL "github.com/Ripple/rippled-packages")

if(DEB)
    include(packaging/deb)
else()
    include(packaging/rpm)
endif()

include(CPack)
