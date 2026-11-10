// Graphics presets on the game's own quality dvars (mw2mp_preset: high, medium, low, very_low). See
// mw2mp_graphics_preset.cpp.

#pragma once

#include <cstdint>

namespace mw2mp::gfx_preset {

// Once a frame (the renderer's ring thread): finds the managed dvars and holds them at the preset's values.
void Apply();

// Mip levels the world's textures start lower at (the renderer's sampler floor); 0 = the game's own.
uint32_t TextureMipsMinus();

}  // namespace mw2mp::gfx_preset
