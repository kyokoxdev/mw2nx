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

# sdmc:/switch/mw2sp/mw2sp.nro (hbmenu shows one NRO per folder): falls back to sdmc:/switch/mw2mp's game/, update/
# and user/ folders (mw2mp/src/mw2mp_app.h); mw2sp_shaders.spvpack/.spvidx beside the NRO.
rexglue_switch_add_nro(mw2sp NAME "mw2nx campaign" AUTHOR "kyokoxdev" VERSION "0.0.1")
