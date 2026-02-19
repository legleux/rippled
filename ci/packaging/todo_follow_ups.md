
# Fix the install() calls to remove ${CMAKE_INSTALL_PREFIX}

In `RippledInstall.cmake` fix these hardcoded paths
```
install (
  TARGETS
    common
   ...
    antithesis-sdk-cpp
  EXPORT RippleExports
  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib
  RUNTIME DESTINATION bin
  INCLUDES DESTINATION include)
```
remove creating links here at all!
```
install(CODE "
  set(CMAKE_MODULE_PATH \"${CMAKE_MODULE_PATH}\")
  include(create_symbolic_link)
  create_symbolic_link(xrpl \
    \${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_INCLUDEDIR}/ripple)
")
```
The Golden Rule for CMake install() paths

Never construct install paths manually.
Always use relative DESTINATION paths + GNUInstallDirs variables.

Correct pattern:

include(GNUInstallDirs)

install(TARGETS xrpld
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

install(DIRECTORY include/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})


Wrong pattern:

install(TARGETS xrpld
        DESTINATION ${CMAKE_INSTALL_PREFIX}/bin)   ❌

install(FILES foo.h
        DESTINATION /usr/include)                 ❌

Why fixing them matters

If install rules are wrong, these break:

scenario	what fails
user installs locally	files go to wrong place
packager overrides prefix	paths incorrect
DESTDIR packaging	paths duplicated
cross-platform builds	layout inconsistent


In

Fix

1) Hardcoded lib/bin/include destinations

This:

LIBRARY DESTINATION lib
ARCHIVE DESTINATION lib
RUNTIME DESTINATION bin
INCLUDES DESTINATION include


works, but it ignores multiarch and platform conventions.

Fix: use GNUInstallDirs variables:

include(GNUInstallDirs)

install(TARGETS ...
  EXPORT RippleExports
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})


Why it matters:

Debian multiarch wants libs in something like lib/x86_64-linux-gnu for /usr prefix builds; CMAKE_INSTALL_LIBDIR handles that when configured appropriately.

Even if you’re installing under /opt/ripple, it keeps the project “packaging-correct.”



2) Manual symlink creation using ${CMAKE_INSTALL_PREFIX} in install(CODE ...)

This is the problematic one:

install(CODE "
  ...
  create_symbolic_link(xrpl \
    \${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_INCLUDEDIR}/ripple)
")



3) You’re installing include/xrpl but linking to include/ripple?

You have:

install(DIRECTORY .../include/xrpl DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})


then your symlink logic references:

.../${CMAKE_INSTALL_INCLUDEDIR}/ripple


That implies you want:

include/ripple -> include/xrpl


(or vice versa). Right now it looks inconsistent / easy to invert by mistake.
If the goal is compatibility with older includes, decide explicitly:
If old code does #include <ripple/...> and new is <xrpl/...>, you want:
include/ripple  ->  include/xrpl
