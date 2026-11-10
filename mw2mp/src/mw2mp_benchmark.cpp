// mw2mp_benchmark (development only, off by default): frame times of a fixed window in a match, and the PGO training
// run's profile dump (mw2mp/build_switch.sh, MW2_PGO=generate). mw2mp has no test pad yet, so nothing drives the
// player: start a private match, leave the controller alone at the spawn, and the window starts kWait seconds after
// cl_ingame turns on.

#include "mw2mp_benchmark.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <vector>

#include <rex/cvar.h>
#include <rex/logging.h>

#include "mw2mp_preset.h"

#if defined(MW2MP_PGO_GENERATE)
#include <sys/stat.h>
#ifndef MW2_APP_NAME
#define MW2_APP_NAME "mw2mp"  // mw2sp's remap header names its own folder
#endif
extern "C" void __gcov_dump(void);
extern "C" void __gcov_reset(void);
#endif

REXCVAR_DEFINE_BOOL(mw2mp_benchmark, false, "MW2",
                    "Development: once a match is loaded (cl_ingame), log the frame times of mw2mp_benchmark_s "
                    "seconds (FPS, percentiles, share over 33.3 / 50 ms). Leave the controller alone at the spawn. "
                    "A PGO training build writes its profile at the end")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_INT32(mw2mp_benchmark_s, 120, "MW2", "Development: seconds of the mw2mp_benchmark window")
    .range(10, 3600)
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

namespace mw2mp::benchmark {
namespace {

using Clock = std::chrono::steady_clock;
constexpr double kWait = 10.0;  // seconds from cl_ingame to the window: spawn, streaming and pipeline compiles settle

std::atomic<uint64_t> g_frames_seen{0};
std::atomic<bool> g_off{false};
// Renderer thread only (AnnotateFrame) from here on.
Clock::time_point g_in_game{};  // first frame with cl_ingame
Clock::time_point g_earlier{};
std::vector<float> g_ms;
bool g_done = false;

void Summarize(double seconds) {
  if (g_ms.empty()) {
    REXLOG_WARN("[benchmark] no frames measured in the window");
    return;
  }
  std::vector<float> ms = g_ms;
  std::sort(ms.begin(), ms.end());
  const auto pct = [&](double p) { return ms[std::min(ms.size() - 1, size_t(p * double(ms.size())))]; };
  size_t over33 = 0, over50 = 0;
  for (float v : ms) {
    over33 += v > 33.4f;
    over50 += v > 50.0f;
  }
  const double n = double(ms.size());
  REXLOG_INFO("[benchmark] {} frames in {:.0f} s: {:.1f} FPS average, median {:.1f} ms ({:.1f} FPS), p90 {:.1f} ms, "
              "p99 {:.1f} ms, worst {:.1f} ms; over 33.3 ms {:.1f} %, over 50 ms {:.1f} %",
              ms.size(), seconds, n / seconds, pct(0.5), 1000.0 / pct(0.5), pct(0.9), pct(0.99), ms.back(),
              100.0 * double(over33) / n, 100.0 * double(over50) / n);
}

}  // namespace

bool IsActive() {
  static const bool active = REXCVAR_GET(mw2mp_benchmark);
  return active && !g_off.load(std::memory_order_relaxed);
}

void Disable() { g_off.store(true, std::memory_order_relaxed); }

uint64_t FramesSeen() { return g_frames_seen.load(std::memory_order_relaxed); }

int AbArm() { return -1; }
int AbArmCached() { return -1; }

void AnnotateFrame() {
  g_frames_seen.fetch_add(1, std::memory_order_relaxed);
  if (g_done || !IsActive()) {
    return;
  }
  const auto now = Clock::now();
  if (g_in_game.time_since_epoch().count() == 0) {
    if (!mw2mp::preset::ClientInGame()) {
      return;
    }
    g_in_game = now;
    REXLOG_INFO("[benchmark] in a match: the {} s window starts in {:.0f} s", REXCVAR_GET(mw2mp_benchmark_s), kWait);
  }
  const double t = std::chrono::duration<double>(now - g_in_game).count() - kWait;
  if (t < 0.0) {
    return;
  }
  static const double duration = double(REXCVAR_GET(mw2mp_benchmark_s));
  if (g_earlier.time_since_epoch().count() == 0) {
#if defined(MW2MP_PGO_GENERATE)
    __gcov_reset();  // the profile covers the window only, not startup and the menus
    REXLOG_INFO("[pgo] counters reset at the start of the window");
#endif
  } else {
    g_ms.push_back(float(std::chrono::duration<double, std::milli>(now - g_earlier).count()));
  }
  g_earlier = now;
  if (t >= duration) {
    g_done = true;
    Summarize(duration);
#if defined(MW2MP_PGO_GENERATE)
    mkdir("/switch/" MW2_APP_NAME "/pgo", 0777);
    __gcov_dump();
    REXLOG_INFO("[pgo] profile written to sdmc:/switch/" MW2_APP_NAME "/pgo: copy that folder to the PC (mw2mp: "
                "data/pgo, mw2sp: data/pgo_sp)");
#endif
  }
}

}  // namespace mw2mp::benchmark
