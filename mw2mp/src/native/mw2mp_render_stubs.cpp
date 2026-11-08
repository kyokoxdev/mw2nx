// Entry points the renderer in native/render/ expects to find outside its own files.
//
// They are optional features of the renderer: reflections drawn on demand, a shadow guard for 30 FPS, a scenery LOD
// hook and a readout of the game's anti-aliasing mode. None of them applies to BO2 as it stands, so each one here
// gives the "feature off" answer. The
// pieces BO2 does need have their own implementations elsewhere: the composite marker in mw2mp_d3d_marker.cpp, the
// blocking ring waits in mw2mp_waits.cpp and the game frame counter in mw2mp_d3d_trace.cpp.

#include <atomic>
#include <cstdint>
#include <string>

#include "render/mw2mp_native_hooks.h"
#include "render/mw2mp_native_render.h"
#include "render/mw2mp_reflection_demand.h"

// Game frames, one per Swap (mw2mp_d3d_trace.cpp); only used for a measurement.
std::atomic<uint64_t> g_mw2mp_frames_game{0};

namespace mw2mp::reflection_demand {
void AnnotateRead() {}
void AnnotateCopy() {}
void AnnotateSwap() {}
bool MeasureVisibility() { return false; }  // never measure: the mirror logic stays off
bool VisibilityChecked() { return false; }
void AnnotateVisible(bool) {}
void AnnotateHidden() {}
void AnnotateWitness(bool) {}
}  // namespace mw2mp::reflection_demand

namespace mw2mp::guard30 {
void Heartbeat(double) {}
void Report() {}
bool NoShadows(bool of_user) { return of_user; }  // only the player's own setting
}  // namespace mw2mp::guard30

namespace mw2mp::render_targets {
// Samples per pixel of the game's AA mode, used to scale occlusion query results. BO2 is drawn without MSAA here,
// so this is always 1.
uint32_t SamplesOriginalModeCurrent(const uint8_t*) { return 1; }
}  // namespace mw2mp::render_targets

namespace mw2mp::scenery_lod {
std::string Summary() { return {}; }
}  // namespace mw2mp::scenery_lod

namespace mw2mp::native {
// ActivateConsumerMarkers and AnnotateCheckMarker are implemented by BO2's marker producer
// (app/src/mw2mp_d3d_marker.cpp). WarnProgressRing wakes the Backend thread's "wait for the ring to drain"
// (app/src/mw2mp_waits.cpp).
}  // namespace mw2mp::native
