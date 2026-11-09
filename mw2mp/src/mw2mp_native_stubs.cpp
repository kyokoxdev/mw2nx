// BO2-only systems the native renderer (src/native, from bo2-360-nx) calls into, answered "feature off" for MW2:
// cheats, presets, the benchmark, GPU skinning, the credits line and the PC tools. Each one moves to a real MW2
// implementation when the renderer needs it (docs/MW2.md, milestone 4).

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <vector>

#include <rex/memory/utils.h>
#include <rex/system/kernel_state.h>

#include "mw2mp_benchmark.h"
#include "mw2mp_cheats.h"
#include "mw2mp_credits.h"
#include "mw2mp_debug_options.h"
#include "mw2mp_debug_watch.h"
#include "mw2mp_gpu_skin.h"
#include "mw2mp_memcpy_callers.h"
#include "mw2mp_pc_profiler.h"
#include "mw2mp_preset.h"
#include "mw2mp_switch.h"
#include "mw2mp_volume_writes.h"
#include "native/render/mw2mp_native_hooks.h"
#include "native/render/mw2mp_native_render.h"

namespace mw2mp {
const char* BuildId() { return "dev"; }
int CoresInUse() { return 3; }
}  // namespace mw2mp

namespace mw2mp::benchmark {
void AnnotateFrame() {}
int AbArm() { return -1; }
int AbArmCached() { return -1; }
}  // namespace mw2mp::benchmark

namespace mw2mp::cheats {
int GetCrosshairOverlays(CrosshairOverlay*, int) { return 0; }
bool InMatch() { return mw2mp::preset::ClientInGame(); }
}  // namespace mw2mp::cheats

namespace mw2mp::credits {
const Picture* Images() { return nullptr; }
bool Place(uint32_t, uint32_t, Placement&) { return false; }
}  // namespace mw2mp::credits

// ponytail: texture write-stamp shortcuts off. Only memcpy/memset stamp pages (mw2mp_crt.cpp); MW2's VMX copies and
// plain-store writers do not, so a skipped texture recheck could go stale. Hook the remaining writers, then turn it
// on. The vertex and index caches are on: their guard checksums the first hits of every copy and marks ranges changed
// without a stamp as always written. The constants shortcut (only the constants a shader reads, from its microcode
// analysis) does not use stamps at all and is on.
namespace mw2mp::debug {
bool TextureShortcutsOff() { return true; }
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

namespace mw2mp::memcpy_callers {
void MarkThread() {}
void Report() {}
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
bool ClientInGame() {
  constexpr uint32_t kClInGameDvar = 0x825A590Cu;
  uint8_t* const base = rex::system::kernel_memory()->virtual_membase();
  const uint32_t dvar = rex::memory::load_and_swap<uint32_t>(base + kClInGameDvar);
  return dvar && base[dvar + 12] != 0;
}
// mw2nx: false. FrontEnd() arms BO2's campaign front-end shortcuts (mw2mp_native_front_end_no_prepass and _reuse);
// answered true, they ran in matches: the prepass skip dropped every colour-masked depth-writing draw, MW2's sun
// shadow maps included (no shadows, surfaces lit flat; ~8,000 draws per 10 s, run 024), and the reuse compared scene
// constants on every draw. MW2MP's menus have no 3D scene to save.
bool FrontEnd() { return false; }
uint32_t MipsMinus() { return 0; }
void NoteFirstVisitCompile() {}
bool OnScreenNote(uint32_t (&)[4]) { return false; }
bool OnScreenBanner(uint32_t (&)[8]) { return false; }
void SetFrontEndSceneKept(bool) {}
}  // namespace mw2mp::preset

namespace mw2mp::volume_writes {
bool UseMarks() { return false; }
void TakeMarks(uint32_t, uint32_t, std::vector<uint32_t>&) {}
void Missed(uint32_t) {}
}  // namespace mw2mp::volume_writes
