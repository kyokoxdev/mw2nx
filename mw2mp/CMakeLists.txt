# mw2mp: Modern Warfare 2 multiplayer (TU6 default_mp.xex), recompiled for the Nintendo Switch.
#
# First light: the SDK's stock ReXApp with its emulated Xenos GPU (GPU_PLUGINS xenos) on NVK. No hooks yet.
# mw2mp_recomp is the generated code (tools/codegen_mw2mp.sh); mw2mp/build_switch.sh configures and builds.

cmake_minimum_required(VERSION 3.25)
project(mw2mp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(generated/rexglue.cmake)

# The native renderer (milestone 4): bo2-360-nx's app/src/native (GPL-3.0, THIRD_PARTY_NOTICES.md), renamed. It reads
# the PM4 ring the game's own D3D layer writes and draws with Vulkan; mw2mp_renderer=native selects it, xenos stays the
# reference. BO2-only pieces it calls into are answered "feature off" in src/mw2mp_native_stubs.cpp.
set(MW2MP_SOURCES
    src/main.cpp
    src/mw2mp_native_stubs.cpp
    # MW2's CRT memcpy/memset as native code ([rexcrt] in overrides.toml)
    src/mw2mp_crt.cpp
    src/native/mw2mp_native_system.cpp
    src/native/mw2mp_render_stubs.cpp
    src/native/render/mw2mp_native_render.cpp
    src/native/render/mw2mp_native_targets.cpp
    src/native/render/mw2mp_native_draws.cpp
    src/native/render/mw2mp_native_deferred.cpp
    src/native/render/mw2mp_native_shaders.cpp
    src/native/render/mw2mp_shader_library.cpp
    src/native/render/mw2mp_shader_index.cpp
    src/native/render/mw2mp_native_progress.cpp
    src/native/render/mw2mp_native_hooks.cpp
    src/native/render/mw2mp_native_texture_pool.cpp
    src/native/render/mw2mp_native_precompile.cpp
    src/native/render/mw2mp_native_capture.cpp
    src/native/render/mw2mp_graphics_settings.cpp
    # The SDK's Xenos shader interpreter, for vertex shaders that write memory (memexport); the Xenos plugin's own
    # copy is not linked into the app.
    ${REXSDK_DIR}/src/graphics/pipeline/shader/interpreter.cpp
    ${REXSDK_DIR}/src/graphics/format/ucode.cpp
    ${REXSDK_DIR}/src/graphics/register_file.cpp
)
add_executable(mw2mp ${MW2MP_SOURCES})
rexglue_setup_target(mw2mp GPU_PLUGINS xenos)

# The renderer draws the game image with the SDK presenter's SPIR-V, fingerprints with xxHash, uses the SDK's Switch
# thread helpers (src/core/threading_switch.h) and the Vulkan headers the SDK links privately.
target_include_directories(mw2mp PRIVATE "${REXSDK_DIR}/src/ui/shaders" "${REXSDK_DIR}/src")
find_path(MW2MP_XXHASH_INCLUDE xxhash.h HINTS "${REXSDK_DIR}/thirdparty/xxHash" NO_CMAKE_FIND_ROOT_PATH REQUIRED)
target_include_directories(mw2mp PRIVATE "${MW2MP_XXHASH_INCLUDE}")
if(TARGET Vulkan::Headers)
    target_link_libraries(mw2mp PRIVATE Vulkan::Headers)
endif()

# REX_GUEST_BARRIER() for the lines tools/guest_barriers.py adds; empty unless MW2MP_GUEST_MEMORY_PLAIN (bo2's header,
# game-agnostic).
option(MW2MP_GUEST_MEMORY_PLAIN "Generated code: plain guest loads and stores, with compiler barriers" OFF)
target_precompile_headers(mw2mp_recomp PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../app/src/bo2zm_guest_memory.h")
if(MW2MP_GUEST_MEMORY_PLAIN)
    target_compile_definitions(mw2mp_recomp PRIVATE BO2ZM_GUEST_MEMORY_PLAIN=1)
endif()

# sdmc:/switch/mw2mp/mw2mp.nro + game/ + update/ + user/ (src/mw2mp_app.h).
rexglue_switch_add_nro(mw2mp NAME "mw2nx" AUTHOR "kyokoxdev" VERSION "0.0.1")
