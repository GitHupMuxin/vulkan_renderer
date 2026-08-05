# ============================================================
# Engine source files — explicitly listed (not GLOB)
#
# Why explicit: CMake GLOB only scans at configure time; adding
# or deleting a .cpp won't trigger re-configuration, leading to
# stale builds. LLVM, Blender, Chromium all use explicit lists
# for the same reason. (UE uses UBT, which rescans every build.)
# ============================================================

# --- Core ---
set(ENGINE_CORE_SRC
    "core/buffer.cpp"
    "core/device.cpp"
    "core/loader.cpp"
    "core/logical_device.cpp"
    "core/physical_device.cpp"
    "core/swapchain.cpp"
)
set(ENGINE_CORE_HEADERS
    "core/buffer.h"
    "core/device.h"
    "core/keycodes.hpp"
    "core/loader.h"
    "core/logical_device.h"
    "core/physical_device.h"
    "core/swapchain.h"
)

# --- Resource ---
set(ENGINE_RESOURCE_SRC
    "resource/environment_lighting.cpp"
    "resource/model.cpp"
    "resource/resource_manager.cpp"
    "resource/texture.cpp"
)
set(ENGINE_RESOURCE_HEADERS
    "resource/environment_lighting.h"
    "resource/model.h"
    "resource/resource_manager.h"
    "resource/texture.h"
)

# --- Scene ---
set(ENGINE_SCENE_SRC
    "scene/camera.cpp"
    "scene/scene.cpp"
)
set(ENGINE_SCENE_HEADERS
    "scene/camera.h"
    "scene/scene.h"
)

# --- Render ---
set(ENGINE_RENDER_SRC
    "render/renderer.cpp"
    "render/render_pass.cpp"
    "render/fullscreen_pass.cpp"
)
set(ENGINE_RENDER_HEADERS
    "render/renderer.h"
    "render/render_pass.h"
    "render/fullscreen_pass.h"
)

# --- Platform ---
set(ENGINE_PLATFORM_SRC
    "platform/window.cpp"
)
set(ENGINE_PLATFORM_HEADERS
    "platform/window.h"
)

# --- Utils ---
set(ENGINE_UTILS_SRC
    "utils/log.cpp"
)
set(ENGINE_UTILS_HEADERS
    "utils/log.h"
    "utils/utils.h"
)

# --- ImGui (exclude imgui_demo.cpp) ---
set(IMGUI_SRC
    "../external/imgui/imgui.cpp"
    "../external/imgui/imgui_draw.cpp"
    "../external/imgui/imgui_widgets.cpp"
)

# --- Basis Universal ---
set(BASISU_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../external/basisu)
set(BASISU_SOURCES
    ${BASISU_DIR}/transcoder/basisu_transcoder.cpp
    ${BASISU_DIR}/zstd/zstd.c
)

# --- Aggregate ---
set(ENGINE_SRC
    ${ENGINE_CORE_SRC}
    ${ENGINE_RESOURCE_SRC}
    ${ENGINE_SCENE_SRC}
    ${ENGINE_RENDER_SRC}
    ${ENGINE_PLATFORM_SRC}
    ${ENGINE_UTILS_SRC}
    ${IMGUI_SRC}
)
set(ENGINE_HEADERS
    ${ENGINE_CORE_HEADERS}
    ${ENGINE_RESOURCE_HEADERS}
    ${ENGINE_SCENE_HEADERS}
    ${ENGINE_RENDER_HEADERS}
    ${ENGINE_PLATFORM_HEADERS}
    ${ENGINE_UTILS_HEADERS}
)

add_library(engine STATIC ${ENGINE_SRC} ${ENGINE_HEADERS} ${BASISU_SOURCES})

target_include_directories(engine PUBLIC ..)

target_link_libraries(engine PUBLIC ${Vulkan_LIBRARY})

