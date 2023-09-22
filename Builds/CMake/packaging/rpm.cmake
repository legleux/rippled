set(CPACK_GENERATOR "RPM")
set(CPACK_RPM_PACKAGE_DEBUG ON)
# set(CPACK_RPM_USER_BINARY_SPECFILE ${CMAKE_SOURCE_DIR}/Builds/CMake/packaging/rpm/rippled.spec)
# set(CPACK_RPM_GENERATE_USER_BINARY_SPECFILE_TEMPLATE ON)
## https://stackoverflow.com/questions/56979738/systemctl-daemon-reload-after-installation-of-rpm

# #install -D ./rippled/Builds/containers/packaging/rpm/50-rippled.preset ${RPM_BUILD_ROOT}/usr/lib/systemd/system-preset/50-rippled.preset
# # /usr/lib/systemd/system-preset/50-rippled.preset
# set(CPACK_RPM_DEBUGINFO_PACKAGE ON)
# set(CPACK_RPM_INSTALL_WITH_EXEC ON)
# set(CPACK_RPM_BUILD_SOURCE_DIRS_PREFIX ${CMAKE_SOURCE_DIR}/..)
# install(FILES ${CMAKE_SOURCE_DIR}/Builds/CMake/packaging/rpm/50-rippled.preset DESTINATION /usr/lib/systemd/system-preset/)

# set(CPACK_RPM_USER_FILELIST
#     "%config /lib/systemd/system/rippled.service"
#     "%config /opt/ripple/bin/rippled"
#     "%config /opt/ripple/etc/rippled.cfg"
# )
# set(CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION
#     /lib /lib/systemd /lib/systemd/system /opt
# )
# PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE ) # 755

set(CPACK_RPM_PACKAGE_ARCHITECTURE "x86_64")
set(CPACK_RPM_INSTALL_WITH_EXEC ON)
set(CPACK_RPM_BUILD_SOURCE_DIRS_PREFIX /tmp)
set(CPACK_RPM_DEBUGINFO_PACKAGE ON)

set(CPACK_GENERATOR RPM)
