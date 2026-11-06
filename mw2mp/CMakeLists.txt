# mw2mp: Modern Warfare 2 multiplayer (TU6 default_mp.xex), recompiled for the Nintendo Switch.
#
# First light: the SDK's stock ReXApp with its emulated Xenos GPU (GPU_PLUGINS xenos) on NVK. No hooks yet.
# mw2mp_recomp is the generated code (tools/codegen_mw2mp.sh); mw2mp/build_switch.sh configures and builds.

cmake_minimum_required(VERSION 3.25)
project(mw2mp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(generated/rexglue.cmake)

add_executable(mw2mp src/main.cpp)
rexglue_setup_target(mw2mp GPU_PLUGINS xenos)

# REX_GUEST_BARRIER() for the lines tools/guest_barriers.py adds; empty unless MW2MP_GUEST_MEMORY_PLAIN (bo2's header,
# game-agnostic).
option(MW2MP_GUEST_MEMORY_PLAIN "Generated code: plain guest loads and stores, with compiler barriers" OFF)
target_precompile_headers(mw2mp_recomp PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../app/src/bo2zm_guest_memory.h")
if(MW2MP_GUEST_MEMORY_PLAIN)
    target_compile_definitions(mw2mp_recomp PRIVATE BO2ZM_GUEST_MEMORY_PLAIN=1)
endif()

# sdmc:/switch/mw2mp/mw2mp.nro + game/ + update/ + user/ (src/mw2mp_app.h).
rexglue_switch_add_nro(mw2mp NAME "mw2nx" AUTHOR "kyokoxdev" VERSION "0.0.1")
