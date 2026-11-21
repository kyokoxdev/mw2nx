// Entry points the renderer in native/render/ expects to find outside its own files.
//
// They are optional features of the renderer: reflections drawn on demand, a shadow guard for 30 FPS, a scenery LOD
// hook and a readout of the game's anti-aliasing mode. None of them is used, so each one here gives the "feature off"
// answer. (bo2-360-nx also had a composite marker, blocking ring waits and a game frame counter of its own; MW2 has
// none of them.)

#include <atomic>
#include <cstdint>
#include <string>

#include "render/mw2mp_native_hooks.h"
#include "render/mw2mp_native_render.h"
#include "render/mw2mp_reflection_demand.h"

// Game frames, one per Swap (counted upstream by a D3D trace; nothing counts them here); only used for a measurement.
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
// Samples per pixel of the game's AA mode, used to scale occlusion query results. The game is drawn without MSAA
// here, so this is always 1.
uint32_t SamplesOriginalModeCurrent(const uint8_t*) { return 1; }
}  // namespace mw2mp::render_targets

namespace mw2mp::scenery_lod {
std::string Summary() { return {}; }
}  // namespace mw2mp::scenery_lod

namespace mw2mp::native {
// ActivateConsumerMarkers, AnnotateCheckMarker and WarnProgressRing belonged to bo2-360-nx's marker producer and
// ring waits; mw2mp_native_stubs.cpp stubs them.
}  // namespace mw2mp::native
