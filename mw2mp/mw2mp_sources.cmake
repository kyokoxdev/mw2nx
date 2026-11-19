# mw2mp's sources but main.cpp, shared with the campaign (mw2sp/CMakeLists.txt builds them with its own addresses).
# Absolute paths so either CMakeLists can include this file; REXSDK_DIR comes from generated/rexglue.cmake.
set(MW2MP_SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/src/mw2mp_native_stubs.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/mw2mp_benchmark.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/mw2mp_user_name.cpp
    # Campaign <-> Multiplayer in the installed program (XamLoaderLaunchTitle restarts into the other NRO)
    ${CMAKE_CURRENT_LIST_DIR}/src/mw2mp_launch.cpp
    # The installed program's first start: its SD card folder and the missing files screen
    ${CMAKE_CURRENT_LIST_DIR}/src/mw2mp_first_start.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/mw2mp_graphics_preset.cpp
    # The game's bounds-tree frustum cull in native code, checked against the game's own
    ${CMAKE_CURRENT_LIST_DIR}/src/mw2mp_native_cull.cpp
    # MW2's CRT memcpy/memset as native code ([rexcrt] in overrides.toml)
    ${CMAKE_CURRENT_LIST_DIR}/src/mw2mp_crt.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/mw2mp_page_writes.cpp
    # The lighting volume's writer, hooked: UpdateVolume takes its marks instead of hashing
    ${CMAKE_CURRENT_LIST_DIR}/src/mw2mp_volume_writes.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/native/mw2mp_native_system.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/native/mw2mp_render_stubs.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/native/render/mw2mp_native_render.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/native/render/mw2mp_native_targets.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/native/render/mw2mp_native_draws.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/native/render/mw2mp_native_deferred.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/native/render/mw2mp_native_shaders.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/native/render/mw2mp_shader_library.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/native/render/mw2mp_shader_index.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/native/render/mw2mp_native_progress.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/native/render/mw2mp_native_hooks.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/native/render/mw2mp_native_texture_pool.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/native/render/mw2mp_native_precompile.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/native/render/mw2mp_native_capture.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/native/render/mw2mp_graphics_settings.cpp
    # The SDK's Xenos shader interpreter, for vertex shaders that write memory (memexport); the Xenos plugin's own
    # copy is not linked into the app.
    ${REXSDK_DIR}/src/graphics/pipeline/shader/interpreter.cpp
    ${REXSDK_DIR}/src/graphics/format/ucode.cpp
    ${REXSDK_DIR}/src/graphics/register_file.cpp
)
