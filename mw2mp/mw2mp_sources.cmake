# mw2mp's sources but main.cpp, shared with the campaign (mw2sp/CMakeLists.txt builds them with its own addresses).
# Absolute paths so either CMakeLists can include this file; REXSDK_DIR comes from generated/rexglue.cmake.
set(MW2MP_SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/src/mw2mp_native_stubs.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/mw2mp_benchmark.cpp
    # MW2's CRT memcpy/memset as native code ([rexcrt] in overrides.toml)
    ${CMAKE_CURRENT_LIST_DIR}/src/mw2mp_crt.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/mw2mp_page_writes.cpp
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
