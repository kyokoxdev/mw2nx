// Graphics presets on the game's own quality dvars (mw2mp_preset: high, medium, low, very_low). See
// mw2mp_graphics_preset.cpp.

#pragma once

#include <atomic>
#include <cstdint>
#include <string>

namespace mw2mp::gfx_preset {

// Once a frame (the renderer's ring thread): finds the managed dvars and holds them at the preset's values.
void Apply();

// The renderer's draw work (ns, scaled from its 1-in-N stopwatch): Apply's adaptive shadows read it.
inline std::atomic<uint64_t> g_draw_work_ns{0};

// Mip levels the world's textures start lower at (the renderer's sampler floor); 0 = the game's own.
uint32_t TextureMipsMinus();

// The level loaded ("mapname" dvar), or empty when not known yet. Ring thread.
std::string MapName();

}  // namespace mw2mp::gfx_preset
