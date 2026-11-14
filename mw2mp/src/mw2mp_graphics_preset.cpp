// Graphics presets (mw2mp_preset = high, medium, low or very_low), modeled on bo2-360-nx's bo2zm_preset.cpp.
//
// A preset is a level for each option below (shadows, dynamic lights, screen effects, decals, corpses and debris,
// texture detail); each level sets the game's own quality dvars, which TU6 still registers although the Xbox 360 has
// no menu for them. High is the game's own values, as the Xbox 360 runs it. The campaign draws twice multiplayer's
// draws a frame (run mw2sp_004: ~6,400 in S.S.D.D., ~10,000 in the next mission, against mp_afghan's ~3,400) with the
// ring thread at 100 %, so draws are what a preset has to remove; BO2's very low halved the game's own.
//
// No console and no hooks: the dvars are found by name in the game's dvar pool and their values written in place
// (Dvar_SetVariant's cheat and write-protect checks only guard the console and scripts). Below high, each managed
// dvar is put back every frame it differs, so a level script resetting one does not undo the preset; switching to
// high restores each one's registered default once and leaves it to the game from then on.
//
// The campaign's dvars (mw2sp/src/mw2sp_game_remap.h, TU6 default.xex):
//   - Dvar_RegisterBool sub_822E15D8 -> Dvar_RegisterVariant sub_822E1548 -> Dvar_FindMalleableVar sub_822E0178 or
//     Dvar_RegisterNew sub_822E12A0. RegisterNew takes dvar_t #count (count at MW2_DVAR_COUNT, at most 4096) from the
//     pool at MW2_DVAR_POOL, 72 bytes each (count * 9 * 8).
//   - dvar_t: name pointer +0, type byte +10 (0 bool, 1 float, 5 int; RegisterNew stb r27,10(r31)), current value
//     +12, latched +28, reset (the registered default) +44, hashNext +68 (FindMalleableVar's lwz r31,68(r31)).
//     A bool's current value is the byte at +12 (MW2_IN_GAME_DVAR reads sv_running there).
// Multiplayer (default_mp.xex TU6): Dvar_RegisterNew sub_8230EAA0 takes dvar_t #count from 0x837A5890 (lis r11,-31878;
// addi r28,r11,22672; r31 = count * 72 + r28; stb r27,10(r31); stw r25,0(r31)) and the count from 0x837A0868
// (lwz r11,2152(r30); cmpwi cr6,r11,4096). Its XModelGetLodForDist is sub_8232E1C0 (the campaign's body, instruction for
// instruction). ai_corpseCount is not registered there; the rest are.

#include "mw2mp_graphics_preset.h"

#include <array>
#include <bit>
#include <atomic>
#include <chrono>
#include <cmath>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

#include <fmt/format.h>
#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/memory/utils.h>
#include <rex/system/kernel_state.h>

#include "mw2mp_preset.h"

#if !(defined(MW2_CAMPAIGN) && MW2_CAMPAIGN)
#define MW2_DVAR_POOL 0x837A5890u
#define MW2_DVAR_COUNT 0x837A0868u
#define MW2_XMODEL_LOD_FOR_DIST 1
#endif

#if defined(MW2_CAMPAIGN) && MW2_CAMPAIGN
#define MW2MP_PRESET_DEFAULT "low"  // run mw2sp_007: very_low's texture mip floor and model detail looked worse
#else
#define MW2MP_PRESET_DEFAULT "high"
#endif

REXCVAR_DEFINE_STRING(mw2mp_preset, MW2MP_PRESET_DEFAULT, "Graphics",
                      "Graphics preset: high (the game's own settings, as the Xbox 360 runs it), medium, low or "
                      "very_low. Lower presets trade draw distance, shadows, dynamic lights, screen effects (depth "
                      "of field, distortion, clouds), decals, corpses and debris, and texture detail for frame rate. "
                      "Applied at once, in menus or in game")
    .allowed({"high", "medium", "low", "very_low"});

// The calibration knob for the draw distance (r_zfar, see kSettings), per level and per player.
REXCVAR_DEFINE_INT32(mw2mp_draw_distance, -1, "Graphics",
                     "Draw distance in game units, where culling fog reaches full opacity and the world past it is not "
                     "drawn (no fog shows on the console). -1 or 0: the level's own (every preset). "
                     "Applied at once")
    .range(-1, 100000);

// The calibration knob for model detail (the hook on XModelGetLodForDist below; campaign only).
REXCVAR_DEFINE_DOUBLE(mw2mp_model_detail, 0.0, "Graphics",
                      "Campaign: how much sooner models switch to their simpler versions (their switch distances are "
                      "divided by this). 0: the preset's (very_low 2, low 1.5, medium 1.25, high 1 = the game's own). A "
                      "model the game draws is never hidden by it, only simplified. Applied at once")
    .range(0.0, 8.0);

// Scene resolution. The game's video setup (campaign sub_8238E5D0, multiplayer sub_823C3DD0; r3 = the video config,
// the IW vidConfig) reads XGetVideoMode and writes the 3D scene size at +8/+12 (1024x600 with HD output, 896x576 SD),
// the display size at +16/+20 (1280x720) and the MSAA samples at +24 (2). Every scene target, viewport and the final
// stretch to the display follow +8/+12, so scaling them here draws fewer pixels with nothing else changed. The 360
// drew MW2 at 1024x600; BO2 at 880x720. Run mw2sp_004: 40 ms of GPU a frame at 768 MHz in the second mission, with
// the fragment ALU as the limit.
#if defined(MW2_CAMPAIGN) && MW2_CAMPAIGN
#define MW2MP_SCENE_SCALE_DEFAULT 85
#else
#define MW2MP_SCENE_SCALE_DEFAULT 100
#endif
REXCVAR_DEFINE_INT32(mw2mp_scene_scale, MW2MP_SCENE_SCALE_DEFAULT, "Graphics",
                     "3D scene resolution in percent of the game's own (1024x600, as the Xbox 360), per axis; the HUD "
                     "and the output stay at 1280x720. 85 = 864x512 (72 % of the pixels), 75 = 768x448. Lower: "
                     "faster on the GPU, blurrier. 100 = the game's own. Needs a restart")
    .range(50, 100)
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// Adaptive shadows and dynamic lights. Run mw2sp_021, favela_escape's market: 5-7k draws a frame, the ring thread's
// draw work ~730 ms/s at 33 FPS (~520 ms/s at 58 FPS elsewhere). The sun's shadow map draws every caster again and each
// dynamic light the surfaces it lights, so they go off while the draw work stays high and come back once it is low.
REXCVAR_DEFINE_BOOL(mw2mp_adaptive_shadows, true, "Graphics",
                    "Shadows and dynamic lights switch off while the renderer is overloaded (draw work above "
                    "mw2mp_adaptive_off_ms for 2 s) and back on once it is light (below mw2mp_adaptive_on_ms for 5 s). "
                    "false = the preset's always");
REXCVAR_DEFINE_INT32(mw2mp_adaptive_off_ms, 650, "Graphics",
                     "Adaptive shadows: draw work in ms per second above which shadows and dynamic lights go off")
    .range(100, 1000);
REXCVAR_DEFINE_INT32(mw2mp_adaptive_on_ms, 450, "Graphics",
                     "Adaptive shadows: draw work in ms per second below which they come back")
    .range(50, 1000);

REXCVAR_DEFINE_INT32(mw2mp_preset_test_s, 0, "BO2",
                     "Test helper: in a level, step to the next lower preset every N seconds (high, medium, low, "
                     "very_low, high, ...), logged as '[preset] test'. 0 = off")
    .range(0, 3600)
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

namespace mw2mp::gfx_preset {
namespace {

constexpr int kLevels = 4;  // 0 high, 1 medium, 2 low, 3 very_low
constexpr const char* kLevelNames[kLevels] = {"high", "medium", "low", "very_low"};
constexpr uint8_t kBool = 0, kFloat = 1, kInt = 5;  // kFloat: the table holds it as a whole number
constexpr int32_t kGame = -1;  // high: the dvar's registered default

struct Setting {
  const char* name;
  uint8_t type;
  int32_t value[kLevels];  // per level; kGame at high
};

// Registered defaults (call sites of sub_822E15D8 bool / sub_822E1620 int(name, value, min, max, flags, desc)):
// sm_enable 1, sm_spotEnable 1, sm_spotLimit 4 (0-4), sm_maxLights 4 (0-4), r_dlightLimit 4 (0-4), r_dof_enable 1,
// r_distortion 1, fx_drawClouds 1, fx_marks 1, fx_marks_smodels 1, fx_marks_ents 1, ai_corpseCount 16 (0-16),
// cg_brass 1, dynEnt_explodeMaxEnts 20 (0-4096).
constexpr Setting kSettings[] = {
    // Shadows: very low has none (the sun's shadow map draws every caster again); low keeps the sun's.
    {"sm_enable", kBool, {kGame, 1, 1, 0}},
    {"sm_spotEnable", kBool, {kGame, 1, 0, 0}},
    {"sm_spotLimit", kInt, {kGame, 1, 0, 0}},
    {"sm_maxLights", kInt, {kGame, 2, 1, 1}},
    // Dynamic lights (muzzle flashes, explosions, effects): each one draws the surfaces it lights again.
    {"r_dlightLimit", kInt, {kGame, 2, 1, 0}},
    // Screen effects.
    {"r_dof_enable", kBool, {kGame, 1, 0, 0}},
    {"r_distortion", kBool, {kGame, 1, 0, 0}},
    {"fx_drawClouds", kBool, {kGame, 1, 0, 0}},
    // Decals: bullet, scorch and blood marks on the world, its static models and characters.
    {"fx_marks", kBool, {kGame, 1, 1, 0}},
    {"fx_marks_smodels", kBool, {kGame, 1, 0, 0}},
    {"fx_marks_ents", kBool, {kGame, 1, 1, 0}},
    // Corpses and debris.
    {"ai_corpseCount", kInt, {kGame, 8, 4, 2}},
    {"cg_brass", kBool, {kGame, 1, 0, 0}},
    {"dynEnt_explodeMaxEnts", kInt, {kGame, 10, 5, 2}},
    // Draw distance: "the distance at which culling fog reaches 100% opacity; 0 is off" (registered float 0, 0 to
    // FLT_MAX, sub_823A2F98). The world fogs out up to it and is culled past it: in run mw2sp_006 the heaviest
    // stretches drew 7-8k draws and 0.7-1.3 M triangles a frame, GPU-bound at 36-51 ms. BO2 cut draws the same way
    // (r_zfar 900-2500 on its zombies maps). mw2mp_draw_distance overrides it.
    // Run mw2sp_007: at 4000 no fog showed on the console (the 360 shaders seem not to draw it), so every preset
    // keeps the level's own; the knob stays for anyone who wants the distance.
    {"r_zfar", kFloat, {kGame, kGame, kGame, kGame}},
};
constexpr size_t kCount = sizeof(kSettings) / sizeof(kSettings[0]);
// Settings the adaptive mode takes to their very_low value: shadows and dynamic lights.
bool Adaptive(const char* name) {
  return std::strncmp(name, "sm_", 3) == 0 || std::strcmp(name, "r_dlightLimit") == 0;
}
bool g_reduced = false;  // adaptive shadows: off now

// Once a second: draw work in ms/s against the thresholds (2 s above to go off, 5 s below to come back).
// Run mw2sp_022: with shadows off the draw work falls well under the threshold to come back, so in a heavy area they
// came back and went off again 4-20 s later (8 of 20 times): shadows blinking. Going off again within 30 s of coming
// back doubles the wait before the next return (5, 10, 20, 40, at most 80 s); a minute on resets it.
void UpdateAdaptive(int level) {
  static auto at = std::chrono::steady_clock::now();
  static uint64_t work_at = g_draw_work_ns.load(std::memory_order_relaxed);
  static int above = 0, below = 0;
  static int wait_s = 5;     // seconds below the threshold before they come back
  static int on_for_s = 60;  // seconds since they came back (a level starts as if long on)
  const auto now = std::chrono::steady_clock::now();
  const double seconds = std::chrono::duration<double>(now - at).count();
  if (seconds < 1.0) {
    return;
  }
  const uint64_t work = g_draw_work_ns.load(std::memory_order_relaxed);
  const double ms = double(work - work_at) / 1e6 / seconds;
  at = now;
  work_at = work;
  if (!REXCVAR_GET(mw2mp_adaptive_shadows) || level >= 3 || !mw2mp::preset::ClientInGame()) {
    above = below = 0;
    g_reduced = false;
    wait_s = 5;
    on_for_s = 60;
    return;
  }
  above = ms > REXCVAR_GET(mw2mp_adaptive_off_ms) ? above + 1 : 0;
  below = ms < REXCVAR_GET(mw2mp_adaptive_on_ms) ? below + 1 : 0;
  if (!g_reduced) {
    if (++on_for_s >= 60) {
      wait_s = 5;
    }
  }
  if (!g_reduced && above >= 2) {
    g_reduced = true;
    if (on_for_s < 30) {
      wait_s = std::min(wait_s * 2, 80);  // back too soon: wait longer next time
    }
    REXLOG_INFO("[preset] adaptive: shadows and dynamic lights OFF (draw work {:.0f} ms/s; back after {} s below)",
                ms, wait_s);
  } else if (g_reduced && below >= wait_s) {
    g_reduced = false;
    on_for_s = 0;
    REXLOG_INFO("[preset] adaptive: shadows and dynamic lights back on (draw work {:.0f} ms/s)", ms);
  }
}
// Texture detail (the renderer's mip floor for the world; UI keeps full detail).
constexpr uint32_t kMipsMinus[kLevels] = {0, 0, 0, 1};

// Model detail (MW2_XMODEL_LOD_FOR_DIST): the factor the hook scales a model's version distance by, per level. 4 at
// very low as BO2's LOW model detail, its very low preset's (there, with draw distance, half the game's draws).
// Run mw2sp_007: at 4 up to 37 % of the picks were simpler than the game's and the switches showed as pop-in, so
// halved.
constexpr float kModelFactors[kLevels] = {1.0f, 1.25f, 1.5f, 2.0f};
std::atomic<float> g_model_factor{1.0f};
std::atomic<uint32_t> g_model_picks{0};    // the hook's calls since the last report
std::atomic<uint32_t> g_model_simpler{0};  // of those, a simpler version than the game's
std::atomic<uint32_t> g_model_held{0};     // of those, held back from the simplest (it has more surfaces)

std::atomic<int> g_level{-1};  // from mw2mp_preset (change callback); -1 until Apply reads it
std::atomic<int> g_applied{0};  // the level in effect (TextureMipsMinus)

int LevelOf(std::string_view name) {
  for (int i = 0; i < kLevels; ++i) {
    if (name == kLevelNames[i]) {
      return i;
    }
  }
  return 0;
}

#ifdef MW2_DVAR_POOL
std::array<uint32_t, kCount> g_dvars{};  // guest address of each managed dvar_t, 0 = not found yet
uint32_t g_scanned_count = 0;
bool g_warned_type[kCount] = {};

// Case-insensitive, as Dvar_FindMalleableVar (I_stricmp).
bool SameName(const uint8_t* base, uint32_t guest, const char* name) {
  if (guest < 0x80000000u) {
    return false;
  }
  const char* s = reinterpret_cast<const char*>(base + guest);
  for (size_t i = 0;; ++i) {
    const char a = s[i] >= 'A' && s[i] <= 'Z' ? char(s[i] + 32) : s[i];
    const char b = name[i] >= 'A' && name[i] <= 'Z' ? char(name[i] + 32) : name[i];
    if (a != b) {
      return false;
    }
    if (!a) {
      return true;
    }
  }
}

// The pool grows while the game registers dvars: scans again whenever the count changed and one is still missing.
void Resolve(const uint8_t* base) {
  const uint32_t count = rex::memory::load_and_swap<uint32_t>(base + MW2_DVAR_COUNT);
  if (count == g_scanned_count || count > 4096) {
    return;
  }
  g_scanned_count = count;
  for (size_t s = 0; s < kCount; ++s) {
    if (g_dvars[s]) {
      continue;
    }
    for (uint32_t i = 0; i < count; ++i) {
      const uint32_t dvar = MW2_DVAR_POOL + i * 72;
      if (SameName(base, rex::memory::load_and_swap<uint32_t>(base + dvar), kSettings[s].name)) {
        g_dvars[s] = dvar;
        break;
      }
    }
  }
}

int32_t ReadValue(const uint8_t* d, uint8_t type, int offset) {
  if (type == kBool) {
    return d[offset];
  }
  if (type == kFloat) {
    return int32_t(std::bit_cast<float>(rex::memory::load_and_swap<uint32_t>(d + offset)));
  }
  return rex::memory::load_and_swap<int32_t>(d + offset);
}

// Writes the value as current and latched (a latched dvar would otherwise switch back on the next restart) and
// marks it modified. false: that dvar is not the type the table says (left alone).
bool Write(uint8_t* base, size_t s, int32_t value, bool only_if_different) {
  uint8_t* const d = base + g_dvars[s];
  if (d[10] != kSettings[s].type) {
    if (!g_warned_type[s]) {
      g_warned_type[s] = true;
      REXLOG_WARN("[preset] {} is of type {} at {:08X}, not {}: left alone", kSettings[s].name, d[10], g_dvars[s],
                  kSettings[s].type);
    }
    return false;
  }
  if (value == kGame) {  // its registered default
    value = ReadValue(d, kSettings[s].type, 44);
  }
  const int32_t current = ReadValue(d, kSettings[s].type, 12);
  if (only_if_different && current == value) {
    return true;
  }
  if (kSettings[s].type == kBool) {
    d[12] = uint8_t(value != 0);
    d[28] = uint8_t(value != 0);
  } else if (kSettings[s].type == kFloat) {
    const uint32_t bits = std::bit_cast<uint32_t>(float(value));
    rex::memory::store_and_swap<uint32_t>(d + 12, bits);
    rex::memory::store_and_swap<uint32_t>(d + 28, bits);
  } else {
    rex::memory::store_and_swap<int32_t>(d + 12, value);
    rex::memory::store_and_swap<int32_t>(d + 28, value);
  }
  d[11] = 1;  // modified
  return true;
}
#endif

}  // namespace

void Apply() {
  static bool watching = false;
  if (!watching) {
    watching = true;
    g_level.store(LevelOf(rex::cvar::GetFlagByName("mw2mp_preset")), std::memory_order_relaxed);
    // The notification arrives with the registry lock held: only the atomic is touched.
    rex::cvar::RegisterChangeCallback("mw2mp_preset", [](std::string_view, std::string_view value) {
      g_level.store(LevelOf(value), std::memory_order_relaxed);
    });
  }
  int level = g_level.load(std::memory_order_relaxed);
  // mw2mp_preset_test_s: steps down every N seconds in a level, from the chosen preset.
  static const int32_t test_s = REXCVAR_GET(mw2mp_preset_test_s);
  if (test_s > 0) {
    static auto since = std::chrono::steady_clock::now();
    static bool in_level = false;
    const bool now_in = mw2mp::preset::ClientInGame();
    if (now_in && !in_level) {
      since = std::chrono::steady_clock::now();
    }
    in_level = now_in;
    if (now_in) {
      const auto steps = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - since)
                             .count() /
                         test_s;
      level = int((level + steps) % kLevels);
    }
  }
  UpdateAdaptive(level);
#ifdef MW2_DVAR_POOL
  static int applied = -1;
  static bool applied_reduced = false;
  static size_t found_logged = 0;
  uint8_t* const base = rex::system::kernel_memory()->virtual_membase();
  Resolve(base);
  size_t found = 0;
  for (size_t s = 0; s < kCount; ++s) {
    if (!g_dvars[s]) {
      continue;
    }
    ++found;
    // Below high, held every frame; high restores the default once, on the change (or when the dvar appears).
    int32_t value = kSettings[s].value[level];
    const bool adaptive = Adaptive(kSettings[s].name);
    if (g_reduced && adaptive) {
      value = kSettings[s].value[3];
    }
    if (std::strcmp(kSettings[s].name, "r_zfar") == 0 && REXCVAR_GET(mw2mp_draw_distance) >= 0) {
      value = REXCVAR_GET(mw2mp_draw_distance);  // 0: the level's own (the registered 0)
    }
    if (value != kGame || level != applied || found > found_logged || (adaptive && g_reduced != applied_reduced)) {
      Write(base, s, value, true);
    }
  }
  if (level != applied || found != found_logged) {
    std::string line;
    for (size_t s = 0; s < kCount; ++s) {
      if (!g_dvars[s]) {
        line += fmt::format(" {} (not registered yet)", kSettings[s].name);
        continue;
      }
      const uint8_t* d = base + g_dvars[s];
      const int32_t v = ReadValue(d, kSettings[s].type, 12);
      line += fmt::format(" {} {}", kSettings[s].name, v);
    }
    REXLOG_INFO("[preset] {}{} ({} of {} dvars found; texture mips -{}):{}", kLevelNames[level],
                test_s > 0 ? " (test)" : "", found, kCount, kMipsMinus[level], line);
    applied = level;
    found_logged = found;
  }
  applied_reduced = g_reduced;
#endif
  g_applied.store(level, std::memory_order_relaxed);
  const double model_detail = REXCVAR_GET(mw2mp_model_detail);
  g_model_factor.store(model_detail > 0.0 ? float(model_detail) : kModelFactors[level], std::memory_order_relaxed);
#ifdef MW2_XMODEL_LOD_FOR_DIST
  static auto report_at = std::chrono::steady_clock::now();
  if (std::chrono::steady_clock::now() - report_at >= std::chrono::seconds(10)) {
    report_at = std::chrono::steady_clock::now();
    const uint32_t picks = g_model_picks.exchange(0, std::memory_order_relaxed);
    if (picks) {
      REXLOG_INFO("[preset] model detail x{:.2f}, last 10 s: {} version picks, {} simpler than the game's ({} held "
                  "back from the simplest)",
                  g_model_factor.load(std::memory_order_relaxed), picks,
                  g_model_simpler.exchange(0, std::memory_order_relaxed),
                  g_model_held.exchange(0, std::memory_order_relaxed));
    }
  }
#endif
}

uint32_t TextureMipsMinus() {
  return kMipsMinus[g_applied.load(std::memory_order_relaxed)];
}

std::string MapName() {
#ifdef MW2_DVAR_POOL
  // "mapname", a string dvar: its current value (+12) points at the name. Set when the server spawns a level.
  static uint32_t dvar = 0, scanned = 0;
  const uint8_t* const base = rex::system::kernel_memory()->virtual_membase();
  if (!dvar) {
    const uint32_t count = rex::memory::load_and_swap<uint32_t>(base + MW2_DVAR_COUNT);
    if (count == scanned || count > 4096) {
      return {};
    }
    scanned = count;
    for (uint32_t i = 0; i < count && !dvar; ++i) {
      const uint32_t d = MW2_DVAR_POOL + i * 72;
      if (SameName(base, rex::memory::load_and_swap<uint32_t>(base + d), "mapname")) {
        dvar = d;
      }
    }
    if (!dvar) {
      return {};
    }
  }
  const uint32_t text = rex::memory::load_and_swap<uint32_t>(base + dvar + 12);
  if (text < 0x80000000u) {
    return {};
  }
  std::string name;
  for (const char* s = reinterpret_cast<const char*>(base + text); *s && name.size() < 64; ++s) {
    const char c = *s;
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')) {
      return {};  // not a file name: not trusted
    }
    name += c;
  }
  return name;
#else
  return {};
#endif
}

}  // namespace mw2mp::gfx_preset

namespace {
// mw2mp_scene_scale: after the game's video setup, its scene size scaled, width in steps of 16 and height in steps of 8
// (Xenos EDRAM tiles), proportions kept.
void ScaleScene(PPCContext& ctx, uint8_t* base, void (*setup)(PPCContext&, uint8_t*)) {
  const uint32_t config = ctx.r3.u32;
  setup(ctx, base);
  static const int32_t scale = REXCVAR_GET(mw2mp_scene_scale);
  if (scale >= 100) {
    return;
  }
  uint8_t* const p = base + config;
  const uint32_t width = rex::memory::load_and_swap<uint32_t>(p + 8);
  const uint32_t height = rex::memory::load_and_swap<uint32_t>(p + 12);
  const uint32_t scaled_width = std::max(16u, (width * uint32_t(scale) + 800) / 1600 * 16);
  const uint32_t scaled_height = std::max(8u, (height * uint32_t(scale) + 400) / 800 * 8);
  rex::memory::store_and_swap<uint32_t>(p + 8, scaled_width);
  rex::memory::store_and_swap<uint32_t>(p + 12, scaled_height);
  REXLOG_INFO("[preset] scene {}x{} -> {}x{} ({} %), display {}x{}", width, height, scaled_width, scaled_height,
              scale, rex::memory::load_and_swap<uint32_t>(p + 16), rex::memory::load_and_swap<uint32_t>(p + 20));
}
}  // namespace

#if defined(MW2_CAMPAIGN) && MW2_CAMPAIGN
REX_EXTERN(__imp__sub_8238E5D0);
REX_HOOK_RAW(sub_8238E5D0) {
  ScaleScene(ctx, base, __imp__sub_8238E5D0);
}
#else
REX_EXTERN(__imp__sub_823C3DD0);
REX_HOOK_RAW(sub_823C3DD0) {
  ScaleScene(ctx, base, __imp__sub_823C3DD0);
}
#endif

#ifdef MW2_XMODEL_LOD_FOR_DIST
// Model detail: the campaign's XModelGetLodForDist, sub_82300B50 (model r3, ramp distance f1, plain distance f2; returns
// the version in r3 or -1). TU6 has no r_lodScale dvars, so the presets apply the factor here, as BO2's
// ChooseModelVersion (bo2zm_preset.cpp) does on its sub_824DE0F8. The game's loop: from maxLoadedLod (byte +224) to
// numLods (byte +225), the first version whose switch distance (float at +64, 40 bytes an entry, XModelLodInfo) is past
// the ramp distance; the last version is checked against the plain distance; -1 past it. Offsets checked against the
// getters beside it: sub_82300AD0 (numsurfs, u16 at +68), sub_82300BD8 (dist at +64), sub_82300BF0 (contents +236,
// so radius +244 and bounds +248, midpoint then half size).
//
// Never hides a model the game draws (the factor alone would drop it past its last version: it keeps its simplest),
// never picks better than the game, and as BO2: a long model (reaching 256 units or more) is measured from its nearest
// possible point, and a model the factor pushes past its versions keeps the second simplest when the simplest has fewer
// surfaces (BO2: far windows lost their glass in the simplest version).
namespace {
float ModelFloat(const uint8_t* p) {
  return std::bit_cast<float>(rex::memory::load_and_swap<uint32_t>(p));
}
float ModelReach(const uint8_t* model) {
  float corner = 0.0f;
  for (int axis = 0; axis < 3; ++axis) {
    const float most = std::fabs(ModelFloat(model + 248 + 4 * axis)) + std::fabs(ModelFloat(model + 260 + 4 * axis));
    corner += most * most;
  }
  const float reach = std::max(ModelFloat(model + 244), std::sqrt(corner));
  return reach < 1e6f ? reach : 0.0f;  // a nonsense value: measured from the origin, as the game does
}

void ChooseModelVersion(PPCContext& ctx, uint8_t* base, void (*game_pick)(PPCContext&, uint8_t*)) {
  const uint32_t model_address = ctx.r3.u32;
  const float ramp = float(ctx.f1.f64);
  game_pick(ctx, base);
  const float factor = mw2mp::gfx_preset::g_model_factor.load(std::memory_order_relaxed);
  const int game = ctx.r3.s32;
  if (factor == 1.0f || game < 0) {
    return;  // the game's own, or the game itself puts it past its versions
  }
  mw2mp::gfx_preset::g_model_picks.fetch_add(1, std::memory_order_relaxed);
  const uint8_t* const model = base + model_address;
  const int lods = int8_t(model[225]);
  if (game >= lods - 1) {
    return;  // already its simplest
  }
  const auto limit = [model](int i) { return ModelFloat(model + 64 + 40 * i); };
  const auto surfaces = [model](int i) { return rex::memory::load_and_swap<uint16_t>(model + 64 + 40 * i + 4); };
  constexpr float kReachLong = 256.0f;
  const float reach = ModelReach(model);
  const float scaled = (reach >= kReachLong ? std::max(ramp - reach, 0.0f) : ramp) * factor;
  int chosen = game;
  while (chosen < lods && !(scaled < limit(chosen))) {
    ++chosen;
  }
  if (chosen >= lods) {
    chosen = lods - 1;
    if (surfaces(lods - 1) < surfaces(lods - 2)) {  // game <= lods - 2 here, so never better than the game
      chosen = lods - 2;
      mw2mp::gfx_preset::g_model_held.fetch_add(1, std::memory_order_relaxed);
    }
  }
  if (chosen != game) {
    mw2mp::gfx_preset::g_model_simpler.fetch_add(1, std::memory_order_relaxed);
  }
  ctx.r3.s64 = chosen;
}
}  // namespace

#if defined(MW2_CAMPAIGN) && MW2_CAMPAIGN
REX_EXTERN(__imp__sub_82300B50);
REX_HOOK_RAW(sub_82300B50) {
  ChooseModelVersion(ctx, base, __imp__sub_82300B50);
}
#else
REX_EXTERN(__imp__sub_8232E1C0);
REX_HOOK_RAW(sub_8232E1C0) {  // multiplayer's XModelGetLodForDist
  ChooseModelVersion(ctx, base, __imp__sub_8232E1C0);
}
#endif
#endif
