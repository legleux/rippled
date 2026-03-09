#[===================================================================[
   Linux packaging support: RPM and Debian targets
#]===================================================================]

if (NOT CMAKE_INSTALL_PREFIX STREQUAL "/opt/xrpld")
    message(STATUS "Packaging targets require -DCMAKE_INSTALL_PREFIX=/opt/xrpld "
                   "(current: '${CMAKE_INSTALL_PREFIX}'); skipping.")
    return()
endif ()

# Generate the RPM spec from template (substitutes @xrpld_version@).
configure_file(${CMAKE_SOURCE_DIR}/package/rpm/xrpld.spec.in
               ${CMAKE_BINARY_DIR}/package/rpm/xrpld.spec @ONLY)

find_program(RPMBUILD_EXECUTABLE rpmbuild)
if (RPMBUILD_EXECUTABLE)
    add_custom_target(package-rpm
                      COMMAND ${CMAKE_SOURCE_DIR}/package/build-pkg.sh rpm ${CMAKE_SOURCE_DIR}
                              ${CMAKE_BINARY_DIR}
                      WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
                      COMMENT "Building RPM package"
                      VERBATIM)
else ()
    message(STATUS "rpmbuild not found; 'package-rpm' target not available")
endif ()

find_program(DPKG_BUILDPACKAGE_EXECUTABLE dpkg-buildpackage)
if (DPKG_BUILDPACKAGE_EXECUTABLE)
    add_custom_target(package-deb
                      COMMAND ${CMAKE_SOURCE_DIR}/package/build-pkg.sh deb ${CMAKE_SOURCE_DIR}
                              ${CMAKE_BINARY_DIR} ${xrpld_version}
                      WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
                      COMMENT "Building Debian package"
                      VERBATIM)
else ()
    message(STATUS "dpkg-buildpackage not found; 'package-deb' target not available")
endif ()
