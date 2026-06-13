# Nintendo Switch/libnx cross toolchain for devkitPro devkitA64.

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

if(NOT DEFINED ENV{DEVKITPRO})
    set(DEVKITPRO "/opt/devkitpro")
else()
    set(DEVKITPRO "$ENV{DEVKITPRO}")
endif()

set(DEVKITA64 "${DEVKITPRO}/devkitA64")
set(LIBNX "${DEVKITPRO}/libnx")
set(PORTLIBS "${DEVKITPRO}/portlibs/switch")

set(CMAKE_C_COMPILER "${DEVKITA64}/bin/aarch64-none-elf-gcc")
set(CMAKE_CXX_COMPILER "${DEVKITA64}/bin/aarch64-none-elf-g++")
set(CMAKE_ASM_COMPILER "${DEVKITA64}/bin/aarch64-none-elf-gcc")
set(CMAKE_AR "${DEVKITA64}/bin/aarch64-none-elf-gcc-ar")
set(CMAKE_RANLIB "${DEVKITA64}/bin/aarch64-none-elf-gcc-ranlib")
set(CMAKE_STRIP "${DEVKITA64}/bin/aarch64-none-elf-strip")

set(CMAKE_FIND_ROOT_PATH ${DEVKITA64} ${DEVKITPRO} ${LIBNX} ${PORTLIBS})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

set(SWITCH_ARCH_FLAGS "-march=armv8-a+crc+crypto -mtune=cortex-a57 -mcpu=cortex-a57 -mtp=soft -ftls-model=local-exec -fPIE")
set(SWITCH_DEFINES "-D__SWITCH__ -D_GNU_SOURCE -D_M_ARM64=1")
set(SWITCH_INCLUDES "-isystem ${LIBNX}/include -isystem ${PORTLIBS}/include -isystem ${DEVKITA64}/include")
set(SWITCH_LINKER_SCRIPT "${CMAKE_SOURCE_DIR}/CMakeModules/SwitchMainTlsBeforeBss.ld")
set(SWITCH_SPECS "${CMAKE_BINARY_DIR}/switch_azahar.specs")
file(WRITE "${SWITCH_SPECS}" "*link:\n+ -T ${SWITCH_LINKER_SCRIPT} -pie --no-dynamic-linker --spare-dynamic-tags=0 --gc-sections -z text -z now -z nodynamic-undefined-weak -z pack-relative-relocs --build-id=sha1 --nx-module-name\n\n*startfile:\ncrti%O%s crtbegin%O%s --require-defined=main\n")

set(SWITCH_COMMON_FLAGS "${SWITCH_ARCH_FLAGS} ${SWITCH_DEFINES} ${SWITCH_INCLUDES} -ffunction-sections -fdata-sections -Wno-psabi")

set(CMAKE_C_FLAGS_INIT "${SWITCH_COMMON_FLAGS}")
set(CMAKE_CXX_FLAGS_INIT "${SWITCH_COMMON_FLAGS}")
set(CMAKE_ASM_FLAGS_INIT "${SWITCH_COMMON_FLAGS}")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-specs=${SWITCH_SPECS} -L${LIBNX}/lib -L${PORTLIBS}/lib -Wl,--gc-sections")
set(CMAKE_C_FLAGS "${SWITCH_COMMON_FLAGS}" CACHE STRING "Switch C flags" FORCE)
set(CMAKE_CXX_FLAGS "${SWITCH_COMMON_FLAGS}" CACHE STRING "Switch C++ flags" FORCE)
set(CMAKE_ASM_FLAGS "${SWITCH_COMMON_FLAGS}" CACHE STRING "Switch ASM flags" FORCE)
set(CMAKE_EXE_LINKER_FLAGS "-specs=${SWITCH_SPECS} -L${LIBNX}/lib -L${PORTLIBS}/lib -Wl,--gc-sections" CACHE STRING "Switch linker flags" FORCE)
set(CMAKE_DL_LIBS "")

set(CMAKE_CROSSCOMPILING TRUE)
set(SWITCH ON CACHE BOOL "Building for Nintendo Switch" FORCE)
set(ANDROID OFF CACHE BOOL "" FORCE)
