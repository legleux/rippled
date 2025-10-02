#[===================================================================[
   install stuff
#]===================================================================]

include(GNUInstallDirs)
# probably included already
include(create_symbolic_link)
include(CMakePackageConfigHelpers)

install(TARGETS
    common
    opts
    ripple_syslibs
    ripple_boost
    xrpl.imports.main
    xrpl.libpb
    xrpl.libxrpl.basics
    xrpl.libxrpl.beast
    xrpl.libxrpl.crypto
    xrpl.libxrpl.json
    xrpl.libxrpl.protocol
    xrpl.libxrpl.resource
    xrpl.libxrpl.ledger
    xrpl.libxrpl.server
    xrpl.libxrpl.net
    xrpl.libxrpl
    antithesis-sdk-cpp
    EXPORT RippleTargets
    RUNTIME DESTINATION  ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION  ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION  ${CMAKE_INSTALL_LIBDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
# install(
#   DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include/xrpl"
#   DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
# )
# after installing headers and configs


# install(CODE "
#   set(CMAKE_MODULE_PATH \"${CMAKE_MODULE_PATH}\")
#   include(create_symbolic_link)
#   create_symbolic_link(xrpl \${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_INCLUDEDIR}/ripple)
# ")

### Try
## https://cmake.org/cmake/help/latest/command/file.html#create-link
## work on windows?
# install(CODE [[
#   file(CREATE_LINK
#     "${CMAKE_INSTALL_PREFIX}/bin/rippled"
#     "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/bin/xrpld"
#     SYMBOLIC)
# ]])

### or
##  Added in version 3.13: Support for creating symlinks on Windows.

# install(CODE [[
#   execute_process(COMMAND "${CMAKE_COMMAND}" -E create_symlink
#     "${CMAKE_INSTALL_PREFIX}/bin/rippled"
#     "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/bin/xrpld")
# ]])

## Try addin gCOPY_ON_ERROR for windows non-admin failure or enable developer mode(best option) or Or grant SeCreateSymbolicLinkPrivilege via Local Security Policy, or run elevated. (untested)


# install (EXPORT RippleExports
#   FILE RippleTargets.cmake
#   NAMESPACE Ripple::
#   DESTINATION lib/cmake/ripple)
install(EXPORT RippleTargets NAMESPACE Ripple:: DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/Ripple")

write_basic_package_version_file (
    RippleConfigVersion.cmake
    VERSION ${rippled_version}
    COMPATIBILITY SameMajorVersion
)

if(is_root_project AND TARGET rippled)
    install(TARGETS rippled RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    set_target_properties(rippled PROPERTIES INSTALL_RPATH_USE_LINK_PATH ON)
  ## Consider this instead: What does link line look like before and after?
  #set_target_properties(rippled PROPERTIES INSTALL_RPATH "$ORIGIN/../lib")

  # sample configs should not overwrite existing files
  # install if-not-exists workaround as suggested by
  # https://cmake.org/Bug/view.php?id=12646

  ## Stop installing config like this. Packaging handles it for package installs, locally just use file().
  # install(CODE "
  #   macro (copy_if_not_exists SRC DEST NEWNAME)
  #     if (NOT EXISTS \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/\${DEST}/\${NEWNAME}\")
  #       file (INSTALL FILE_PERMISSIONS OWNER_READ OWNER_WRITE DESTINATION \"\${CMAKE_INSTALL_PREFIX}/\${DEST}\" FILES \"\${SRC}\" RENAME \"\${NEWNAME}\")
  #     else ()
  #       message (\"-- Skipping : \$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/\${DEST}/\${NEWNAME}\")
  #     endif ()
  #   endmacro()
  #   copy_if_not_exists(\"${CMAKE_CURRENT_SOURCE_DIR}/cfg/rippled-example.cfg\" etc rippled.cfg)
  #   copy_if_not_exists(\"${CMAKE_CURRENT_SOURCE_DIR}/cfg/validators-example.txt\" etc validators.txt)
  # ")

    file(GLOB CONFIGURE_DEPENDS cfg_files "${CMAKE_SOURCE_DIR}/cfg/*-example.*")
    foreach(src IN LISTS cfg_files)
        get_filename_component(name "${src}" NAME)
        string(REPLACE "-example" "" dst "${name}")

        # install-time script (quoted, no raw brackets)
        install(CODE
            "set(_dst \"\$ENV{DESTDIR}${CMAKE_INSTALL_FULL_SYSCONFDIR}/${dst}\")
                if(NOT EXISTS \"\${_dst}\")
                  file(INSTALL
                    DESTINATION \"${CMAKE_INSTALL_FULL_SYSCONFDIR}\"
                    FILES \"${src}\"
                    RENAME \"${dst}\"
                    FILE_PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ)
                endif()")
    endforeach()
  # install(CODE "
  #   set(CMAKE_MODULE_PATH \"${CMAKE_MODULE_PATH}\")
  #   include(create_symbolic_link)
  #   create_symbolic_link(rippled${suffix} \
  #     \${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}/xrpld${suffix})
  # ")
  #   install(CODE [[
  #       file(CREATE_LINK
  #           rippled${suffix}
  #           "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/bin/xrpld"
  #           SYMBOLIC
  #       )
  #       file(CREATE_LINK
  #           rippled.cfg
  #           "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/etc/xrpld.cfg"
  #           SYMBOLIC
  #       )
  # ]])
    install(CODE
        "file(MAKE_DIRECTORY \"\$ENV{DESTDIR}${CMAKE_INSTALL_FULL_BINDIR}\")
         file(CREATE_LINK \"rippled${suffix}\"
              \"\$ENV{DESTDIR}${CMAKE_INSTALL_FULL_BINDIR}/xrpld\" SYMBOLIC)

         file(MAKE_DIRECTORY \"\$ENV{DESTDIR}${CMAKE_INSTALL_FULL_SYSCONFDIR}\")
         file(CREATE_LINK \"rippled.cfg\"
              \"\$ENV{DESTDIR}${CMAKE_INSTALL_FULL_SYSCONFDIR}/xrpld.cfg\" SYMBOLIC)
        ")
        #  maybe add COPY_ON_ERROR on Windows?
    #[===[
    Maybe better to create some simple scripts. I hate these escapes like \"\$ENV{DESTDIR}
    example of above as: cmake/install_symlinks.cmake.in

    file(MAKE_DIRECTORY "$ENV{DESTDIR}@CMAKE_INSTALL_FULL_BINDIR@")
    file(CREATE_LINK "rippled@SUFFIX@" "$ENV{DESTDIR}@CMAKE_INSTALL_FULL_BINDIR@/xrpld" SYMBOLIC)

    file(MAKE_DIRECTORY "$ENV{DESTDIR}@CMAKE_INSTALL_FULL_SYSCONFDIR@")
    file(CREATE_LINK "rippled.cfg" "$ENV{DESTDIR}@CMAKE_INSTALL_FULL_SYSCONFDIR@/xrpld.cfg" SYMBOLIC)

    # headers dir example
    file(MAKE_DIRECTORY "$ENV{DESTDIR}@CMAKE_INSTALL_FULL_INCLUDEDIR@")
    file(CREATE_LINK "rippled" "$ENV{DESTDIR}@CMAKE_INSTALL_FULL_INCLUDEDIR@/xrpld" SYMBOLIC)

    Then in RippledInstall.cmake
    set(SUFFIX "")

    configure_file(
      ${CMAKE_SOURCE_DIR}/cmake/install_symlinks.cmake.in
      ${CMAKE_CURRENT_BINARY_DIR}/install_symlinks.cmake
      @ONLY
    )

    install(SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/install_symlinks.cmake")


    ]===]
    install(FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/RippleConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/RippleConfigVersion.cmake"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/Ripple"
    )
endif()
