// mw2mp_benchmark (development only, off by default; mw2mp_benchmark.cpp): once player 0 is in a match, the log gets
// one line with the frame times of a fixed window: average and median FPS, percentiles, and the share of frames over
// 33.3 and 50 ms. A PGO training build (MW2_PGO=generate) writes its profile to sdmc:/switch/mw2mp/pgo at the end.

#pragma once

#include <cstdint>

namespace mw2mp::benchmark {

// mw2mp_benchmark is on.
bool IsActive();
// No benchmark in this start whatever the settings say. Before the match.
void Disable();

// Each frame the game presents (the native renderer's Swap).
void AnnotateFrame();
// How many frames AnnotateFrame has seen since the start (for a frame watchdog).
uint64_t FramesSeen();

// bo2's 2x2 A/B inside the benchmark route (the native renderer's *_ab cvars read it). Not ported: always -1.
int AbArm();
int AbArmCached();

}  // namespace mw2mp::benchmark
