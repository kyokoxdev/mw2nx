// A sampling profiler for the PC build (mw2mp_pc_profile, off by default; see mw2mp_pc_profiler.cpp). Nothing on the
// Switch, which has its own (sdk/src/ui/switch_perf.cpp).
#pragma once

namespace mw2mp::pc_profiler {

// Starts the sampling thread if mw2mp_pc_profile is set.
void Start();

// A frame was presented (the native renderer's Present): mw2mp_pc_profile_stalls keeps only the samples taken while
// no frame has been presented for a while.
void NoteFrame();

}  // namespace mw2mp::pc_profiler
