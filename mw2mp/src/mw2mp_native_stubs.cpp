// BO2-only systems the native renderer (src/native, from bo2-360-nx) calls into, answered "feature off" for MW2:
// cheats, presets, the benchmark, GPU skinning, the credits line and the PC tools. Each one moves to a real MW2
// implementation when the renderer needs it (docs/MW2.md, milestone 4).

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <vector>

#include "mw2mp_benchmark.h"
#include "mw2mp_cheats.h"
#include "mw2mp_credits.h"
#include "mw2mp_debug_options.h"
#include "mw2mp_debug_watch.h"
#include "mw2mp_gpu_skin.h"
#include "mw2mp_memcpy_callers.h"
#include "mw2mp_page_writes.h"
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
bool InMatch() { return false; }
}  // namespace mw2mp::cheats

namespace mw2mp::credits {
const Picture* Images() { return nullptr; }
bool Place(uint32_t, uint32_t, Placement&) { return false; }
}  // namespace mw2mp::credits

// ponytail: every write-stamp shortcut off (textures, constants, vertex/index caches): BO2 stamps pages from its copy
// routines' hooks and MW2 has none yet, so a cached copy could go stale. Correct but slower; hook MW2's memcpy/memset
// callers and turn them back on.
namespace mw2mp::debug {
bool TextureShortcutsOff() { return true; }
bool ConstantsShortcutOff() { return true; }
bool CachesOff() { return true; }
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

// The stamps themselves (bo2-360-nx's bo2zm_page_writes.cpp), never enabled: see mw2mp::debug above.
namespace mw2mp::page_writes {
std::atomic<bool> g_enabled{false};
std::atomic<uint32_t> g_epoch{1};
std::atomic<uint32_t> g_stamps[1u << 17] = {};
std::atomic<uint32_t> g_regions[1u << 13] = {};
std::atomic<bool> g_use_regions{true};
void MarkPhysical(uint32_t, uint32_t) {}
void MarkVolatile(uint32_t, uint32_t) {}
void NextEpoch() { g_epoch.fetch_add(1, std::memory_order_relaxed); }
void Enable(bool) {}  // stays off until MW2's writers mark pages
}  // namespace mw2mp::page_writes

namespace mw2mp::pc_profiler {
void NoteFrame() {}
}  // namespace mw2mp::pc_profiler

namespace mw2mp::preset {
bool ClientInGame() { return false; }
bool FrontEnd() { return true; }
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
