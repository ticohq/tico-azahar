set(TICO_SHARED_DIR ${PROJECT_SOURCE_DIR}/tico)

add_executable(tico
    clocks.cpp
    clocks.h
    emu_window_switch.cpp
    emu_window_switch.h
    game_overlay.cpp
    game_overlay.h
    main.cpp
    saves.cpp
    saves.h
    switch_keyboard.cpp
    switch_keyboard.h
    switch_compat.cpp
    switch_libnx.h
    tico_settings.cpp
    tico_settings.h
    # tico's overlay, shared with the other cores
    ${TICO_SHARED_DIR}/UsbStorage.cpp
    ${TICO_SHARED_DIR}/UsbStorage.h
    ${TICO_SHARED_DIR}/overlay/imgui_overlay.cpp
    ${TICO_SHARED_DIR}/overlay/imgui_overlay.h
    ${TICO_SHARED_DIR}/overlay/overlay_renderer.h
    ${TICO_SHARED_DIR}/overlay/overlay_ui.cpp
    ${TICO_SHARED_DIR}/overlay/overlay_ui.h
    ${TICO_SHARED_DIR}/overlay/ra_alerts.cpp
    ${TICO_SHARED_DIR}/overlay/ra_alerts.h
    ${TICO_SHARED_DIR}/overlay/tico_config.cpp
    ${TICO_SHARED_DIR}/overlay/tico_config.h
    ${TICO_SHARED_DIR}/overlay/translation_manager.cpp
    ${TICO_SHARED_DIR}/overlay/translation_manager.h
)

create_target_directory_groups(tico)
set_target_properties(tico PROPERTIES OUTPUT_NAME "azahar-switch.elf")
target_compile_definitions(tico PRIVATE __SWITCH__)

target_include_directories(tico PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/.. ${TICO_SHARED_DIR}
    ${TICO_SHARED_DIR}/deps/usbhsfs/include)

if(NOT DEVKITPRO)
    if(DEFINED ENV{DEVKITPRO})
        set(DEVKITPRO "$ENV{DEVKITPRO}")
    else()
        set(DEVKITPRO "/opt/devkitpro")
    endif()
endif()

link_directories(${DEVKITPRO}/portlibs/switch/lib ${DEVKITPRO}/libnx/lib)

set(SWITCH_VULKAN_LIBRARY "" CACHE FILEPATH "Optional override for the Switch libvulkan.a archive")
if(SWITCH_VULKAN_LIBRARY)
    set(SWITCH_VULKAN_LIB "${SWITCH_VULKAN_LIBRARY}")
else()
    find_library(SWITCH_VULKAN_LIB
        NAMES vulkan
        PATHS
            "$ENV{SWITCH_VULKAN_ROOT}"
            "/opt/nvk-switch/lib"
            "/nvk-build/src/nouveau/vulkan"
            "${DEVKITPRO}/portlibs/switch/lib"
        NO_DEFAULT_PATH
    )
endif()

if(NOT SWITCH_VULKAN_LIB)
    message(FATAL_ERROR "Switch Vulkan frontend requires libvulkan.a. Set SWITCH_VULKAN_LIBRARY or SWITCH_VULKAN_ROOT.")
endif()

# Older NVK builds need libdrm_nouveau; the portlibs one needs expat instead (see its vulkan.pc)
find_library(SWITCH_DRM_NOUVEAU_LIB NAMES drm_nouveau PATHS "${DEVKITPRO}/portlibs/switch/lib" NO_DEFAULT_PATH)
find_library(SWITCH_EXPAT_LIB NAMES expat PATHS "${DEVKITPRO}/portlibs/switch/lib" NO_DEFAULT_PATH)
foreach(_lib SWITCH_DRM_NOUVEAU_LIB SWITCH_EXPAT_LIB)
    if(NOT ${_lib})
        set(${_lib} "")
    endif()
endforeach()

target_link_libraries(tico PRIVATE
    citra_common
    citra_core
    audio_core
    video_core
    network
    input_common
    json-headers
    imgui
    stb
    tsl::robin_map
    ${PLATFORM_LIBRARIES}
    Threads::Threads
    GLESv2
    glapi
    ${SWITCH_VULKAN_LIB}
    ${SWITCH_DRM_NOUVEAU_LIB}
    ${SWITCH_EXPAT_LIB}
    # libusbhsfs (FAT/exFAT) that also reads NTFS, read-only, through usbntfs;
    # by path so portlibs' FAT-only copy is not picked up instead, and before libnx
    ${TICO_SHARED_DIR}/deps/usbhsfs/lib/libusbhsfs.a
    ${TICO_SHARED_DIR}/deps/usbhsfs/lib/libusbntfs.a
    z
    nx
    m
)

target_link_options(tico PRIVATE "LINKER:--allow-multiple-definition")
