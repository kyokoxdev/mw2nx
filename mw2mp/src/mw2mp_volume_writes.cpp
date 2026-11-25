// The model lighting volume's writer, hooked (mw2mp_native_volume_by_writes). Port of bo2-360-nx's
// bo2zm_volume_writes.cpp.
//
// Why: UpdateVolume (mw2mp_native_draws.cpp) found the pages the game changes in the tiled 3D lighting volume by
// hashing all of it each update: XXH3 was 4.2 % of the ring thread in campaign run mw2sp_018 (2.1 % in MP).
// The store watch build (MW2_WATCH_STORES, run mw2sp_020: dcburning and contingency, 30 min) saw a single writer:
// every store into the volume came from sub_8239F480, called from 823A0618. mw2mp's twin is sub_823F7DB8 (the same
// instructions).
//
// sub_8239F480(r3 = source, r4 = alpha byte, r5 = destination): packs 64 RGB byte triples into 8_8_8_8 words and stores
// them as eight runs of 32 bytes at r5 + 0, 256, 1024, 1280, 4096, 4352, 5120 and 5376 (one brick of the tiled volume,
// [r5, r5 + 5408)); r5 is never changed. The hook runs it, then marks the physical pages of that range. UpdateVolume
// takes the marks instead of hashing. The mark comes after the stores, so a page whose mark the renderer took is read
// after the write.
//
// The check (in UpdateVolume): the first 300 updates by marks, then one in 64, also hash every page. A page that
// changed without a mark (another writer) is logged and the marks are off for a while: 30 s after the first miss, 60 s
// after the second, 120 s after the third, then hashing again checks the next 300 looks in full. The fourth miss turns
// them off for the rest of the run. Runs mw2sp_022/024/036/038 had one miss each (a late mark near a level change),
// after which hashing cost 2-4 % of the ring thread for the rest of the session.

#include "mw2mp_volume_writes.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <string>

#include <fmt/format.h>
#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/memory/utils.h>
#include <rex/runtime.h>
#include <rex/system/xmemory.h>

REXCVAR_DEFINE_BOOL(mw2mp_native_volume_by_writes, true, "MW2",
                    "Native renderer: the lighting volume's changed pages come from a hook on the game function that "
                    "writes it, instead of hashing it on every update (checked against the hash). false = hashing")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

#if defined(MW2_CAMPAIGN) && MW2_CAMPAIGN
#define MW2_VOLUME_WRITER sub_8239F480
#define MW2_VOLUME_WRITER_IMP __imp__sub_8239F480
#else
#define MW2_VOLUME_WRITER sub_823F7DB8
#define MW2_VOLUME_WRITER_IMP __imp__sub_823F7DB8
#endif

REX_EXTERN(MW2_VOLUME_WRITER_IMP);

// The volume's second writer: sub_823B93D0 (r3 = brick index, r4 = 4 color bytes) fills one brick with one color, 64
// word stores at r9 + table[i] (r9 = the volume's base + r3's tiled offset, the table at 0x83E49300). The misses of
// runs mw2sp_048-052 were bricks written with no call of sub_8239F480 running or recent on the page; the store watch
// (run mw2sp_051) caught this function storing into the volume. mw2mp's twin is sub_823F2810 (same instructions,
// table at 0x83D8D700).
#if defined(MW2_CAMPAIGN) && MW2_CAMPAIGN
#define MW2_VOLUME_FILL sub_823B93D0
#define MW2_VOLUME_FILL_IMP __imp__sub_823B93D0
#else
#define MW2_VOLUME_FILL sub_823F2810
#define MW2_VOLUME_FILL_IMP __imp__sub_823F2810
#endif

REX_EXTERN(MW2_VOLUME_FILL_IMP);

namespace mw2mp::volume_writes {
namespace {

constexpr uint32_t kPhysicalPages = 0x20000000u >> 12;  // 512 MB of guest physical memory
constexpr uint32_t kBrickBytes = 5408;                   // the last run ends at r5 + 5376 + 32

std::array<std::atomic<uint64_t>, kPhysicalPages / 64> g_marks{};
constexpr uint32_t kMissesForSession = 4;  // the miss that turns the marks off for the rest of the run
std::atomic<uint32_t> g_misses{0};
std::atomic<int64_t> g_resume_ns{0};  // steady_clock ns: no marks before this; INT64_MAX = rest of the run

int64_t NowNs() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

// Diagnostic for the misses (runs mw2sp_048-050: 4 of 4 in every invasion run, each one brick): a writer call
// preempted between its stores and its mark, or another writer? The calls running now (destination, start), the last
// 256 that ended, and the first call after a miss that covers the missed page, all logged at or after the miss.
struct InFlight {
  std::atomic<uint32_t> destination{0};  // guest r5; 0 = free
  std::atomic<int64_t> start_ns{0};
};
std::array<InFlight, 16> g_in_flight;
struct Ended {
  std::atomic<uint32_t> physical{0};
  std::atomic<int64_t> end_ns{0};
};
std::array<Ended, 256> g_ended;
std::atomic<uint32_t> g_ended_next{0};
std::atomic<uint32_t> g_missed_page{0};  // physical page index + 1 of the last miss until a call covers it; 0 = none
std::atomic<int64_t> g_missed_ns{0};

uint32_t Physical(uint32_t guest_address) {
  auto* runtime = rex::Runtime::instance();
  auto* memory = runtime ? runtime->memory() : nullptr;
  return memory ? memory->GetPhysicalAddress(guest_address) : UINT32_MAX;
}

bool Covers(uint32_t physical, uint32_t page) {
  return physical != UINT32_MAX && page >= ((physical & 0x1FFFFFFFu) >> 12) &&
         page <= (((physical & 0x1FFFFFFFu) + kBrickBytes - 1) >> 12);
}

bool On() {
  static const bool on = REXCVAR_GET(mw2mp_native_volume_by_writes);
  return on;
}

void Mark(uint32_t physical, uint32_t bytes = kBrickBytes) {
  if (physical == UINT32_MAX) {
    return;  // not physical memory: not the volume (the check would catch a miss)
  }
  const uint32_t first = (physical & 0x1FFFFFFFu) >> 12;
  const uint32_t last = std::min<uint32_t>(((physical & 0x1FFFFFFFu) + bytes - 1) >> 12, kPhysicalPages - 1);
  for (uint32_t p = first; p <= last; ++p) {
    g_marks[p >> 6].fetch_or(uint64_t(1) << (p & 63), std::memory_order_release);
  }
}

}  // namespace

bool UseMarks() { return On() && NowNs() >= g_resume_ns.load(std::memory_order_relaxed); }

void TakeMarks(uint32_t physical_first, uint32_t count, std::vector<uint32_t>& pages) {
  const uint32_t first = (physical_first & 0x1FFFFFFFu) >> 12;
  const uint32_t end = std::min<uint32_t>(first + count, kPhysicalPages);
  for (uint32_t w = first >> 6; w <= (end - 1) >> 6 && first < end; ++w) {
    // Only this range's bits: other pages' marks stay for whoever asks for them.
    uint64_t mask = ~uint64_t(0);
    if (w == first >> 6) {
      mask &= ~uint64_t(0) << (first & 63);
    }
    if (w == (end - 1) >> 6 && (end & 63) != 0) {
      mask &= ~(~uint64_t(0) << (end & 63));
    }
    uint64_t bits = g_marks[w].fetch_and(~mask, std::memory_order_acquire) & mask;
    while (bits) {
      const uint32_t b = uint32_t(__builtin_ctzll(bits));
      bits &= bits - 1;
      pages.push_back(w * 64 + b - first);
    }
  }
}

void Missed(uint32_t physical_page, const uint64_t runs[2], uint64_t frames) {
  const int64_t now = NowNs();
  if (now < g_resume_ns.load(std::memory_order_relaxed)) {
    return;  // already off: more suspects of the same look are the same event
  }
  const uint32_t page = (physical_page & 0x1FFFFFFFu) >> 12;
  std::string running;
  for (InFlight& f : g_in_flight) {
    const uint32_t destination = f.destination.load(std::memory_order_acquire);
    if (destination) {
      const uint32_t physical = Physical(destination);
      running += fmt::format(" {:08X} (physical {:08X}{}, {:.1f} ms)", destination, physical,
                             Covers(physical, page) ? ", THIS PAGE" : "",
                             double(now - f.start_ns.load(std::memory_order_relaxed)) / 1e6);
    }
  }
  int64_t last_end = 0;
  uint32_t last_physical = 0;
  for (Ended& e : g_ended) {
    const uint32_t physical = e.physical.load(std::memory_order_relaxed);
    const int64_t end = e.end_ns.load(std::memory_order_relaxed);
    if (physical && Covers(physical, page) && end > last_end) {
      last_end = end;
      last_physical = physical;
    }
  }
  REXLOG_WARN("[native] C3 lighting volume miss detail: page {:08X}, seen changed {} frames before; changed 32-byte "
              "runs {:016X}{:016X} (bit n = page offset n x 32, high word first); writer calls running:{}; the last "
              "of 256 ended calls on this page: {}",
              physical_page, frames, runs[1], runs[0], running.empty() ? " none" : running.c_str(),
              last_end ? fmt::format("{:08X}, ended {:.1f} ms ago", last_physical, double(now - last_end) / 1e6)
                       : std::string("none"));
  g_missed_ns.store(now, std::memory_order_relaxed);
  g_missed_page.store(page + 1, std::memory_order_release);
  const uint32_t misses = g_misses.fetch_add(1, std::memory_order_relaxed) + 1;
  if (misses >= kMissesForSession) {
    g_resume_ns.store(INT64_MAX, std::memory_order_relaxed);
    REXLOG_ERROR("[native] C3 lighting volume: page {:08X} changed without a mark from the writer hook (miss {}); "
                 "its pages are found by hashing for the rest of the session",
                 physical_page, misses);
    return;
  }
  const int64_t seconds = int64_t(30) << (misses - 1);
  g_resume_ns.store(now + seconds * 1000000000, std::memory_order_relaxed);
  REXLOG_WARN("[native] C3 lighting volume: page {:08X} changed without a mark from the writer hook (miss {} of {}); "
              "found by hashing for {} s, then by the marks again",
              physical_page, misses, kMissesForSession, seconds);
}

}  // namespace mw2mp::volume_writes

// The game's own code, then the marks of what it wrote.
REX_HOOK_RAW(MW2_VOLUME_WRITER) {
  using namespace mw2mp::volume_writes;
  const uint32_t destination = ctx.r5.u32;
  if (!On()) {
    MW2_VOLUME_WRITER_IMP(ctx, base);
    return;
  }
  InFlight* slot = nullptr;  // none free (16 running at once): this call is just not listed
  for (InFlight& f : g_in_flight) {
    uint32_t expected = 0;
    if (f.destination.load(std::memory_order_relaxed) == 0 &&
        f.destination.compare_exchange_strong(expected, destination, std::memory_order_acq_rel)) {
      f.start_ns.store(NowNs(), std::memory_order_relaxed);
      slot = &f;
      break;
    }
  }
  MW2_VOLUME_WRITER_IMP(ctx, base);
  const uint32_t physical = Physical(destination);
  Mark(physical);
  const int64_t end = NowNs();
  if (slot) {
    slot->destination.store(0, std::memory_order_release);
  }
  Ended& e = g_ended[g_ended_next.fetch_add(1, std::memory_order_relaxed) & 255];
  e.physical.store(physical == UINT32_MAX ? 0 : physical, std::memory_order_relaxed);
  e.end_ns.store(end, std::memory_order_relaxed);
  if (const uint32_t missed = g_missed_page.load(std::memory_order_acquire);
      missed && Covers(physical, missed - 1) && g_missed_page.exchange(0, std::memory_order_relaxed) == missed) {
    REXLOG_WARN("[native] C3 lighting volume miss detail: the first writer call on page {:08X} since the miss: {:08X} "
                "(physical {:08X}), ended {:.1f} ms after the miss",
                (missed - 1) << 12, destination, physical,
                double(end - g_missed_ns.load(std::memory_order_relaxed)) / 1e6);
  }
}

// The game's fill, then the marks of the brick it filled. After the call r9 still holds the brick's address and r11 the
// table's last entry (lwzu); the marked range spans the table's lowest to highest offset.
REX_HOOK_RAW(MW2_VOLUME_FILL) {
  using namespace mw2mp::volume_writes;
  MW2_VOLUME_FILL_IMP(ctx, base);
  if (!On()) {
    return;
  }
  const uint32_t brick = ctx.r9.u32;
  const uint8_t* const table = base + (ctx.r11.u32 - 63 * 4);
  uint32_t lo = UINT32_MAX;
  uint32_t hi = 0;
  for (uint32_t i = 0; i < 64; ++i) {
    const uint32_t offset = rex::memory::load_and_swap<uint32_t>(table + 4 * i);
    lo = std::min(lo, offset);
    hi = std::max(hi, offset);
  }
  Mark(Physical(brick + lo), hi + 4 - lo);
}
