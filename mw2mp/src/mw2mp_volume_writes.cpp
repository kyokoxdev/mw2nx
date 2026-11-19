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
// changed without a mark (another writer) is logged, and the marks are off for the rest of the run.

#include "mw2mp_volume_writes.h"

#include <algorithm>
#include <array>
#include <atomic>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
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

namespace mw2mp::volume_writes {
namespace {

constexpr uint32_t kPhysicalPages = 0x20000000u >> 12;  // 512 MB of guest physical memory
constexpr uint32_t kBrickBytes = 5408;                   // the last run ends at r5 + 5376 + 32

std::array<std::atomic<uint64_t>, kPhysicalPages / 64> g_marks{};
std::atomic<bool> g_missed{false};

bool On() {
  static const bool on = REXCVAR_GET(mw2mp_native_volume_by_writes);
  return on;
}

void Mark(uint32_t guest_address) {
  auto* runtime = rex::Runtime::instance();
  auto* memory = runtime ? runtime->memory() : nullptr;
  if (!memory) {
    return;
  }
  const uint32_t physical = memory->GetPhysicalAddress(guest_address);
  if (physical == UINT32_MAX) {
    return;  // not physical memory: not the volume (the check would catch a miss)
  }
  const uint32_t first = (physical & 0x1FFFFFFFu) >> 12;
  const uint32_t last = std::min<uint32_t>(((physical & 0x1FFFFFFFu) + kBrickBytes - 1) >> 12, kPhysicalPages - 1);
  for (uint32_t p = first; p <= last; ++p) {
    g_marks[p >> 6].fetch_or(uint64_t(1) << (p & 63), std::memory_order_release);
  }
}

}  // namespace

bool UseMarks() { return On() && !g_missed.load(std::memory_order_relaxed); }

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

void Missed(uint32_t physical_page) {
  if (!g_missed.exchange(true)) {
    REXLOG_ERROR("[native] C3 lighting volume: page {:08X} changed without a mark from the writer hook (another "
                 "writer); its pages are found by hashing for the rest of the session",
                 physical_page);
  }
}

}  // namespace mw2mp::volume_writes

// The game's own code, then the marks of what it wrote.
REX_HOOK_RAW(MW2_VOLUME_WRITER) {
  const uint32_t destination = ctx.r5.u32;
  MW2_VOLUME_WRITER_IMP(ctx, base);
  if (mw2mp::volume_writes::On()) {
    mw2mp::volume_writes::Mark(destination);
  }
}
