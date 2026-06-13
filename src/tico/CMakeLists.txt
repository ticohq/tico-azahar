add_executable(tico
    emu_window_switch.cpp
    emu_window_switch.h
    main.cpp
    switch_keyboard.cpp
    switch_keyboard.h
    switch_compat.cpp
    switch_libnx.h
    overlay/overlay_ui.cpp
    overlay/overlay_ui.h
    overlay/tico_config.cpp
    overlay/tico_config.h
    overlay/translation_manager.cpp
    overlay/translation_manager.h
    overlay/vulkan_overlay.cpp
    overlay/vulkan_overlay.h
)

create_target_directory_groups(tico)
set_target_properties(tico PROPERTIES OUTPUT_NAME "azahar-switch.elf")
target_compile_definitions(tico PRIVATE __SWITCH__)

target_include_directories(tico PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)

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
    drm_nouveau
    z
    nx
    m
)

target_link_options(tico PRIVATE "LINKER:--allow-multiple-definition")
