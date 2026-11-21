// Upstream systems the native renderer (src/native, from bo2-360-nx) calls into, answered "feature off" for MW2:
// cheats, presets, GPU skinning, the credits line and the PC tools. Each one moves to a real MW2
// implementation when the renderer needs it (docs/MW2.md, milestone 4).

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <vector>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/memory/utils.h>
#include <rex/platform.h>
#include <rex/system/kernel_state.h>

#include "mw2mp_cheats.h"
#include "mw2mp_credits.h"
#include "mw2mp_debug_options.h"
#include "mw2mp_debug_watch.h"
#include "mw2mp_gpu_skin.h"
#include "mw2mp_graphics_preset.h"
#include "mw2mp_memcpy_callers.h"
#include "mw2mp_pc_profiler.h"
#include "mw2mp_preset.h"
#include "mw2mp_switch.h"
#include "mw2mp_volume_writes.h"
#include "native/render/mw2mp_native_hooks.h"
#include "native/render/mw2mp_native_render.h"

// Core 3 (sdk/src/ui/switch_core3.cpp), as bo2-360-nx's bo2zm_switch.cpp: only acts when started from a forwarder that
// allows cores 0-3 (sphaira: CPU Cores 4); from hbmenu core 3 is not the process's and nothing moves. BO2 measured
// +12-16 % with the Server on all four cores and the audio threads alone on core 3. MW2's mixer is "Audio Worker".
REXCVAR_DEFINE_INT32(mw2mp_cpu_cores, 4, "MW2",
                     "Switch: 4 = the threads mw2mp_core3_threads names also use the fourth CPU core, which only a "
                     "forwarder made with four cores allows (sphaira: CPU Cores 4); started any other way it changes "
                     "nothing. 3 = the three cores every launch has")
    .range(3, 4)
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
// The renderer's vertex copy thread (cores 2-3, its own priority 0x2E): on cores 0-2 beside the game threads it ran
// ~0.3 % and the ring did 2/3 of the copies itself (run mw2sp_019).
REXCVAR_DEFINE_STRING(mw2mp_core3_threads,
                      "Server:0xF:59,Audio Worker:0x8:43,XMA Decoder:0x8:43,MW2 vertex copies:0xC:46", "MW2",
                      "Switch, 4-core forwarder only: threads moved to core 3, as comma-separated \"name prefix[:core "
                      "mask[:priority]]\" (0x8 = core 3 alone, 0xF = cores 0-3). \"\" = none moved")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

#if REX_PLATFORM_SWITCH
extern "C" void RexSwitchCore3SetLog(void (*log)(const char*));
extern "C" bool RexSwitchCore3Configure(const char* rules, bool clamp, bool meter, bool active);
extern "C" bool RexSwitchCore3Active(void);
extern "C" void RexSwitchCore3SetHogLimit(int percent, int seconds);
#endif

namespace mw2mp {
const char* BuildId() { return "dev"; }
int CoresInUse() {
#if REX_PLATFORM_SWITCH
  return RexSwitchCore3Active() ? 4 : 3;
#else
  return 3;
#endif
}
// Before the game's threads start (OnPostInitLogging), so each one a rule names is moved as it names itself. Failsafe
// as BO2's defaults: back to three cores when our threads hold core 3 over 85 % for 2 s.
// XAudio mixes on an unnamed guest thread (sub_820A5828 starts it at one of these entries; the campaign's in
// mw2sp_game_remap.h) that the Audio Worker's callback only takes finished frames from. At the guest priority on full
// cores 0-2 it fell behind: run mw2sp_020 (dcburning) had up to 660 requests without data per 10 s (5.3 ms of silence
// each, the crackling), 74 per interval below 40 fps against 2 above 57. At the audio priority it preempts the game
// threads for its ~10-14 % of a core. A value from the toml wins.
#ifndef MW2_XAUDIO_THREADS
#define MW2_XAUDIO_THREADS "0x820A56B0,0x820A5778"
#endif
void ConfigureCores() {
#if REX_PLATFORM_SWITCH
  if (rex::cvar::GetFlagByName("switch_audio_guest_threads").empty()) {
    rex::cvar::SetFlagByName("switch_audio_guest_threads", MW2_XAUDIO_THREADS);
  }
  RexSwitchCore3SetLog([](const char* text) { REXLOG_INFO("{}", text); });
  RexSwitchCore3SetHogLimit(85, 2);
  RexSwitchCore3Configure(REXCVAR_GET(mw2mp_core3_threads).c_str(), true, true, REXCVAR_GET(mw2mp_cpu_cores) == 4);
#endif
}
}  // namespace mw2mp

namespace mw2mp::cheats {
int GetCrosshairOverlays(CrosshairOverlay*, int) { return 0; }
bool InMatch() { return mw2mp::preset::ClientInGame(); }
}  // namespace mw2mp::cheats

namespace mw2mp::credits {
const Picture* Images() { return nullptr; }
bool Place(uint32_t, uint32_t, Placement&) { return false; }
}  // namespace mw2mp::credits

// Texture write-stamp shortcuts on, as bo2 (its default). memcpy/memset and the VMX copy kernels (mw2mp_crt.cpp), file
// reads and memexport stamp pages; a writer that does not stamp is caught by the guard (every recheck for the first
// 4,000, then 1 in 16, or 1 in 4 while young), logged as "changed without a stamp", and its pages count as written from
// then on.
// The vertex and index caches are on: their guard checksums the first hits of every copy and marks ranges changed
// without a stamp as always written. The constants shortcut (only the constants a shader reads, from its microcode
// analysis) does not use stamps at all and is on.
namespace mw2mp::debug {
// Run mw2sp_018 (on) went wrong (black/green viewmodel, then a crash) while file reads and the VMX copy kernels did
// not stamp; both stamp now (page_writes::Enable, mw2mp_crt.cpp). Plain-store writers (Bink frames) stay on the guard.
bool TextureShortcutsOff() { return false; }
bool ConstantsShortcutOff() { return false; }
bool CachesOff() { return false; }
void WatchPhysicalPages(const std::vector<uint32_t>&, Watch, const std::vector<uint64_t>*) {}
bool WatchVertices() { return false; }
}  // namespace mw2mp::debug

namespace mw2mp::gpu_skin {
void JobRef::Release() {}  // New() is never called here: no job exists
bool Physical(uint32_t, uint32_t&) { return false; }
bool RendererOn() { return false; }
void SetRendererReady(bool) {}
bool MayOverlap(uint32_t, uint32_t) { return false; }
void Overlapping(uint32_t, uint32_t, std::vector<JobRef>&) {}
void Materialize(const Job&) {}
RendererCounts& Counts() {
  static RendererCounts counts;
  return counts;
}
void Refused(const char*, uint32_t, uint32_t, uint32_t, uint32_t) {}
void Compare(const uint8_t*, const uint8_t*, uint32_t, uint32_t, uint32_t) {}
}  // namespace mw2mp::gpu_skin

// Weak: the MW2SP_MEMCPY_CALLERS test build links the real ones (mw2mp_memcpy_callers.cpp).
namespace mw2mp::memcpy_callers {
__attribute__((weak)) void MarkThread() {}
__attribute__((weak)) void Report() {}
}  // namespace mw2mp::memcpy_callers

namespace mw2mp::native {
void ActivateConsumerMarkers(bool) {}  // no marker producer: FlushState always reads the ring
void AnnotateCheckMarker(bool, uint32_t, uint32_t, uint32_t, uint32_t) {}
void WarnProgressRing() {}  // wakes BO2's Backend ring-drain wait; MW2 has no such hook
}  // namespace mw2mp::native


namespace mw2mp::pc_profiler {
void NoteFrame() {}
}  // namespace mw2mp::pc_profiler

namespace mw2mp::preset {
// cl_ingame, read from the game's own dvar (no hook). TU6: Dvar_RegisterBool is sub_8230EE00 (r3 name, r4 default,
// r5 flags, r6 description); sub_82187A98 registers "cl_ingame" (0x8201A0BC) and stores the dvar_t* at 0x825A590C.
// The current value is the byte at +12: the game itself sets cl_ingame to 1 when the connection state is 9 (active)
// and the byte reads 0, and back to 0 otherwise (lwz r3,22796(r11); lbz r10,12(r3) near 0x82185120). false until the
// dvar is registered.
// The campaign (mw2sp/src/mw2sp_game_remap.h) has no cl_ingame and names its own dvar_t* here.
#ifndef MW2_IN_GAME_DVAR
#define MW2_IN_GAME_DVAR 0x825A590Cu
#endif
bool ClientInGame() {
  constexpr uint32_t kClInGameDvar = MW2_IN_GAME_DVAR;
  uint8_t* const base = rex::system::kernel_memory()->virtual_membase();
  const uint32_t dvar = rex::memory::load_and_swap<uint32_t>(base + kClInGameDvar);
#ifdef MW2_CGAME_FLAG
  if (!base[MW2_CGAME_FLAG]) {
    return false;  // the campaign: a level is loading or none is loaded (mw2sp_game_remap.h)
  }
#endif
  return dvar && base[dvar + 12] != 0;
}
// mw2nx: false. FrontEnd() arms BO2's campaign front-end shortcuts (mw2mp_native_front_end_no_prepass and _reuse);
// answered true, they ran in matches: the prepass skip dropped every colour-masked depth-writing draw, MW2's sun
// shadow maps included (no shadows, surfaces lit flat; ~8,000 draws per 10 s, run 024), and the reuse compared scene
// constants on every draw. MW2MP's menus have no 3D scene to save.
bool FrontEnd() { return false; }
uint32_t MipsMinus() { return mw2mp::gfx_preset::TextureMipsMinus(); }
void NoteFirstVisitCompile() {}
bool OnScreenNote(uint32_t (&)[4]) { return false; }
bool OnScreenBanner(uint32_t (&)[8]) { return false; }
void SetFrontEndSceneKept(bool) {}
}  // namespace mw2mp::preset

