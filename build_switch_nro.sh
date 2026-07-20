#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build_switch"
MESA_NVK_DIR="${MESA_NVK_DIR:-/nvk-build}"
DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
TOOLCHAIN_FILE="${SCRIPT_DIR}/CMakeModules/SwitchToolchain.cmake"

if [ "${1:-}" = "clean" ]; then
    rm -rf "${BUILD_DIR}"
fi

mkdir -p "${BUILD_DIR}"

CMAKE_EXTRA_ARGS=()
if [ -n "${SWITCH_VULKAN_LIBRARY:-}" ]; then
    CMAKE_EXTRA_ARGS+=("-DSWITCH_VULKAN_LIBRARY=${SWITCH_VULKAN_LIBRARY}")
elif [ -f "/opt/nvk-switch/lib/libvulkan.a" ]; then
    CMAKE_EXTRA_ARGS+=("-DSWITCH_VULKAN_LIBRARY=/opt/nvk-switch/lib/libvulkan.a")
elif [ -f "${MESA_NVK_DIR}/src/nouveau/vulkan/libvulkan.a" ]; then
    CMAKE_EXTRA_ARGS+=("-DSWITCH_VULKAN_LIBRARY=${MESA_NVK_DIR}/src/nouveau/vulkan/libvulkan.a")
fi

cmake -B "${BUILD_DIR}" "${SCRIPT_DIR}" \
    -Wno-dev \
    -DCMAKE_TOOLCHAIN_FILE="${TOOLCHAIN_FILE}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DSWITCH=ON \
    -DENABLE_QT=OFF \
    -DENABLE_SDL2=OFF \
    -DENABLE_LIBRETRO=OFF \
    -DENABLE_OPENGL=OFF \
    -DENABLE_VULKAN=ON \
    -DENABLE_SOFTWARE_RENDERER=OFF \
    -DENABLE_TESTS=OFF \
    -DENABLE_WEB_SERVICE=OFF \
    -DENABLE_SCRIPTING=OFF \
    -DENABLE_CUBEB=OFF \
    -DENABLE_OPENAL=OFF \
    -DENABLE_LIBUSB=OFF \
    -DENABLE_ROOM=OFF \
    -DENABLE_ROOM_STANDALONE=OFF \
    -DUSE_DISCORD_PRESENCE=OFF \
    -DCITRA_USE_PRECOMPILED_HEADERS=OFF \
    -DCITRA_WARNINGS_AS_ERRORS=OFF \
    -DENABLE_LTO=OFF \
    "${CMAKE_EXTRA_ARGS[@]}"

cmake --build "${BUILD_DIR}" --target tico -j"$(nproc)"

ELF_FILE="${BUILD_DIR}/src/tico/azahar-switch.elf"
if [ ! -f "${ELF_FILE}" ]; then
    ELF_FILE="$(find "${BUILD_DIR}" -type f \( -name 'azahar-switch.elf' -o -name 'azahar-switch' \) | head -1)"
fi

if [ -z "${ELF_FILE}" ] || [ ! -f "${ELF_FILE}" ]; then
    echo "ERROR: azahar-switch ELF not found"
    exit 1
fi

NACP_FILE="${BUILD_DIR}/azahar-switch.nacp"
NRO_FILE="${SCRIPT_DIR}/azahar-switch.nro"
TICO_NRO_FILE="${SCRIPT_DIR}/tico-azahar.nro"
ROMFS_DIR="${BUILD_DIR}/tico_romfs"
nacptool --create "tico Azahar" "ticoverse.com" "1.0.6" "${NACP_FILE}"
rm -rf "${ROMFS_DIR}"
mkdir -p "${ROMFS_DIR}"
cp -R "${SCRIPT_DIR}/src/tico/fonts" "${ROMFS_DIR}/fonts"
cp -R "${SCRIPT_DIR}/src/tico/lang" "${ROMFS_DIR}/lang"
cp -R "${SCRIPT_DIR}/src/tico/assets" "${ROMFS_DIR}/assets"
if [ -d "${SCRIPT_DIR}/src/tico/config" ]; then
    cp -R "${SCRIPT_DIR}/src/tico/config" "${ROMFS_DIR}/config"
fi
cp "${ELF_FILE}" "${BUILD_DIR}/azahar-switch.debug.elf"
"${DEVKITPRO}/devkitA64/bin/aarch64-none-elf-strip" --strip-all "${ELF_FILE}"
elf2nro "${ELF_FILE}" "${NRO_FILE}" --nacp="${NACP_FILE}" --romfsdir="${ROMFS_DIR}"
cp "${NRO_FILE}" "${TICO_NRO_FILE}"

if [ -n "${SWITCH_SD_ROOT:-}" ]; then
    mkdir -p "${SWITCH_SD_ROOT}/switch" "${SWITCH_SD_ROOT}/tico/cores"
    cp "${NRO_FILE}" "${SWITCH_SD_ROOT}/switch/azahar-switch.nro"
    cp "${TICO_NRO_FILE}" "${SWITCH_SD_ROOT}/tico/cores/tico-azahar.nro"
    echo "Installed: ${SWITCH_SD_ROOT}/switch/azahar-switch.nro"
    echo "Installed: ${SWITCH_SD_ROOT}/tico/cores/tico-azahar.nro"
fi

echo "Output: ${NRO_FILE}"
echo "Output: ${TICO_NRO_FILE}"
echo "Copy standalone to SD as: /switch/azahar-switch.nro"
echo "Copy Tico chainload core to SD as: /tico/cores/tico-azahar.nro"
echo "ROM fallback path: sdmc:/tico/system/3ds/game.zcci"
echo "Boot markers: sdmc:/tico/system/3ds/azahar_boot.txt and sdmc:/tico/system/3ds/debug/startup.txt"
