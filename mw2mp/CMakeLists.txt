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
include(mw2mp_sources.cmake)
add_executable(mw2mp src/main.cpp ${MW2MP_SOURCES})
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

# Profile-guided optimization (mw2mp/build_switch.sh, MW2_PGO; bo2-360-nx's recipe): the generated code and the app's
# hot native code (the renderer, the native system, the CRT copies, the page stamps). Not the SDK: bo2's training
# build that instrumented the SDK's platform layer hung at startup. The profile's file names are the object paths
# relative to the build folder (-fprofile-prefix-path), so the training build (out/switch-pgo) and the final one
# (out/switch) look for the same names.
set(MW2MP_PGO "" CACHE STRING "Switch: profile-guided optimization: generate, use or empty")
set(MW2MP_PGO_DIR "" CACHE PATH "Switch, MW2MP_PGO=use: the folder with the training run's profile")
if(MW2MP_PGO)
    set(_mw2mp_pgo_native
        src/mw2mp_crt.cpp src/mw2mp_page_writes.cpp src/native/mw2mp_native_system.cpp
        src/native/render/mw2mp_native_render.cpp src/native/render/mw2mp_native_targets.cpp
        src/native/render/mw2mp_native_draws.cpp src/native/render/mw2mp_native_deferred.cpp
        src/native/render/mw2mp_native_shaders.cpp src/native/render/mw2mp_shader_library.cpp
        src/native/render/mw2mp_native_hooks.cpp src/native/render/mw2mp_native_texture_pool.cpp)
    if(MW2MP_PGO STREQUAL "generate")
        # -fno-profile-values: devkitA64's libgcov reaches the indirect-call profiler's thread-locals through
        # tpidr_el0, which is 0 on Horizon threads (bo2 faulted at 0xd68 in __gcov_indirect_call_profiler_v4).
        set(_mw2mp_pgo_flags -fprofile-generate=/switch/mw2mp/pgo -fno-profile-values -fprofile-update=single
            "-fprofile-prefix-path=${CMAKE_BINARY_DIR}")
        target_compile_definitions(mw2mp PRIVATE MW2MP_PGO_GENERATE=1)
        target_link_libraries(mw2mp PRIVATE gcov)
    elseif(MW2MP_PGO STREQUAL "use")
        set(_mw2mp_pgo_flags "-fprofile-use=${MW2MP_PGO_DIR}" "-fprofile-prefix-path=${CMAKE_BINARY_DIR}"
            -fprofile-partial-training -fprofile-correction -Wno-missing-profile -Wno-coverage-mismatch)
    else()
        message(FATAL_ERROR "MW2MP_PGO must be generate, use or empty")
    endif()
    target_compile_options(mw2mp_recomp PRIVATE ${_mw2mp_pgo_flags})
    set_property(SOURCE ${_mw2mp_pgo_native} APPEND PROPERTY COMPILE_OPTIONS ${_mw2mp_pgo_flags})
endif()

# sdmc:/switch/mw2mp/mw2mp.nro + game/ + update/ + user/ (src/mw2mp_app.h).
rexglue_switch_add_nro(mw2mp NAME "mw2nx" AUTHOR "kyokoxdev" VERSION "0.0.1")
