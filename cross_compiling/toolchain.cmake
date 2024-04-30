# the name of the target operating system
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

# Don't know what this should be
#set(CMAKE_SYSROOT /home/devel/rasp-pi-rootfs)
# set(CMAKE_STAGING_PREFIX /home/devel/stage)
set(CROSS_ROOT /usr/bin)
# set(CROSS_TOOLS /usr/)

set(CMAKE_AR                        ${CROSS_ROOT}/aarch64-linux-gnu-ar)
set(CMAKE_ASM_COMPILER              ${CROSS_ROOT}/aarch64-linux-gnu-as)
set(CMAKE_C_COMPILER                ${CROSS_ROOT}/aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER              ${CROSS_ROOT}/aarch64-linux-gnu-g++)
set(CMAKE_LINKER                    ${CROSS_ROOT}/aarch64-linux-gnu-ld)
set(CMAKE_OBJCOPY                   ${CROSS_ROOT}/aarch64-linux-gnu-objcopy)
set(CMAKE_RANLIB                    ${CROSS_ROOT}/aarch64-linux-gnu-ranlib)
set(CMAKE_SIZE                      ${CROSS_ROOT}/aarch64-linux-gnu-size)
set(CMAKE_STRIP                     ${CROSS_ROOT}/aarch64-linux-gnu-strip)
set(CMAKE_GCOV                      ${CROSS_ROOT}/aarch64-linux-gnu-gcov)
#CMAKE_SYSROOT

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
