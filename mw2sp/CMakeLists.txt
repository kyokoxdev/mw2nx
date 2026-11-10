# mw2sp: Modern Warfare 2 campaign (TU6 default.xex), recompiled for the Nintendo Switch.
#
# The same program as mw2mp (its native renderer, native CRT, app and settings), built from mw2mp/src unchanged with
# the campaign's addresses forced in (src/mw2sp_game_remap.h). mw2sp_recomp is the generated code
# (tools/codegen_mw2sp.sh); mw2sp/build_switch.sh configures and builds.

cmake_minimum_required(VERSION 3.25)
project(mw2sp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(generated/rexglue.cmake)

set(MW2MP_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../mw2mp/src")
include(../mw2mp/mw2mp_sources.cmake)
add_executable(mw2sp src/main.cpp ${MW2MP_SOURCES})
rexglue_setup_target(mw2sp GPU_PLUGINS xenos)
target_compile_options(mw2sp PRIVATE -include "${CMAKE_CURRENT_SOURCE_DIR}/src/mw2sp_game_remap.h")

# As mw2mp/CMakeLists.txt: mw2mp/src's headers, the SDK presenter's SPIR-V, xxHash and the Vulkan headers.
target_include_directories(mw2sp PRIVATE "${MW2MP_SRC}" "${REXSDK_DIR}/src/ui/shaders" "${REXSDK_DIR}/src")
find_path(MW2SP_XXHASH_INCLUDE xxhash.h HINTS "${REXSDK_DIR}/thirdparty/xxHash" NO_CMAKE_FIND_ROOT_PATH REQUIRED)
target_include_directories(mw2sp PRIVATE "${MW2SP_XXHASH_INCLUDE}")
if(TARGET Vulkan::Headers)
    target_link_libraries(mw2sp PRIVATE Vulkan::Headers)
endif()

# REX_GUEST_BARRIER() for the lines tools/guest_barriers.py adds; empty unless MW2SP_GUEST_MEMORY_PLAIN (bo2's header,
# game-agnostic).
option(MW2SP_GUEST_MEMORY_PLAIN "Generated code: plain guest loads and stores, with compiler barriers" OFF)
target_precompile_headers(mw2sp_recomp PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../app/src/bo2zm_guest_memory.h")
if(MW2SP_GUEST_MEMORY_PLAIN)
    target_compile_definitions(mw2sp_recomp PRIVATE BO2ZM_GUEST_MEMORY_PLAIN=1)
endif()

# Profile-guided optimization, as mw2mp/CMakeLists.txt (mw2sp/build_switch.sh, MW2_PGO): the generated code and the
# app's hot native code, not the SDK. The training build writes sdmc:/switch/mw2sp/pgo at the end of the
# mw2mp_benchmark window; object paths are relative to the build folder, so out/switch-pgo and out/switch match.
set(MW2SP_PGO "" CACHE STRING "Switch: profile-guided optimization: generate, use or empty")
set(MW2SP_PGO_DIR "" CACHE PATH "Switch, MW2SP_PGO=use: the folder with the training run's profile")
if(MW2SP_PGO)
    set(_mw2sp_pgo_native)
    foreach(_f mw2mp_crt.cpp mw2mp_page_writes.cpp native/mw2mp_native_system.cpp
               native/render/mw2mp_native_render.cpp native/render/mw2mp_native_targets.cpp
               native/render/mw2mp_native_draws.cpp native/render/mw2mp_native_deferred.cpp
               native/render/mw2mp_native_shaders.cpp native/render/mw2mp_shader_library.cpp
               native/render/mw2mp_native_hooks.cpp native/render/mw2mp_native_texture_pool.cpp)
        list(APPEND _mw2sp_pgo_native "${MW2MP_SRC}/${_f}")
    endforeach()
    if(MW2SP_PGO STREQUAL "generate")
        # -fno-profile-values: devkitA64's libgcov reaches the indirect-call profiler's thread-locals through
        # tpidr_el0, which is 0 on Horizon threads (bo2 faulted at 0xd68 in __gcov_indirect_call_profiler_v4).
        set(_mw2sp_pgo_flags -fprofile-generate=/switch/mw2sp/pgo -fno-profile-values -fprofile-update=single
            "-fprofile-prefix-path=${CMAKE_BINARY_DIR}")
        target_compile_definitions(mw2sp PRIVATE MW2MP_PGO_GENERATE=1)
        target_link_libraries(mw2sp PRIVATE gcov)
    elseif(MW2SP_PGO STREQUAL "use")
        set(_mw2sp_pgo_flags "-fprofile-use=${MW2SP_PGO_DIR}" "-fprofile-prefix-path=${CMAKE_BINARY_DIR}"
            -fprofile-partial-training -fprofile-correction -Wno-missing-profile -Wno-coverage-mismatch)
    else()
        message(FATAL_ERROR "MW2SP_PGO must be generate, use or empty")
    endif()
    target_compile_options(mw2sp_recomp PRIVATE ${_mw2sp_pgo_flags})
    set_property(SOURCE ${_mw2sp_pgo_native} APPEND PROPERTY COMPILE_OPTIONS ${_mw2sp_pgo_flags})
endif()

# sdmc:/switch/mw2sp/mw2sp.nro (hbmenu shows one NRO per folder): falls back to sdmc:/switch/mw2mp's game/, update/
# and user/ folders (mw2mp/src/mw2mp_app.h); mw2sp_shaders.spvpack/.spvidx beside the NRO.
rexglue_switch_add_nro(mw2sp NAME "mw2nx campaign" AUTHOR "kyokoxdev" VERSION "0.0.1")
