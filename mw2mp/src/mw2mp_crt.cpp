// MW2's memory copy and fill routines as native code ([rexcrt] in mw2mp/overrides.toml), as bo2-360-nx's
// app/src/bo2zm_crt.cpp.
//
// The codegen drops the recompiled bodies of mw2mp_memcpy (0x8241A9F0) and mw2mp_memset (0x8241A890) and calls these
// from every direct call and the dispatch table. r3 is left as it came in, which is dst: what both routines return.
// No other register is written (the 360 routines only clobber volatile ones). Copies use memmove semantics, a
// superset of the CRT's forward-only memcpy. Each write is stamped for the native renderer (mw2mp_page_writes.h).

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/memory/utils.h>
#include <rex/system/xmemory.h>

#include "mw2mp_page_writes.h"

namespace {

inline uint8_t* Host(uint8_t* base, uint32_t guest) { return rex::memory::GuestPtr<uint8_t*>(base, guest); }

}  // namespace

// r3 = dst, r4 = src, r5 = byte count.
extern "C" REX_FUNC(rexcrt_mw2mp_memcpy) {
  const uint32_t n = ctx.r5.u32;
  if (n != 0) [[likely]] {
    std::memmove(Host(base, ctx.r3.u32), Host(base, ctx.r4.u32), n);
    mw2mp::page_writes::MarkGuest(ctx.r3.u32, n);
  }
}

// r3 = dst, r4 = value (its low byte), r5 = byte count.
extern "C" REX_FUNC(rexcrt_mw2mp_memset) {
  const uint32_t n = ctx.r5.u32;
  if (n != 0) [[likely]] {
    std::memset(Host(base, ctx.r3.u32), int(ctx.r4.u32 & 0xFF), n);
    mw2mp::page_writes::MarkGuest(ctx.r3.u32, n);
  }
}

// MW2's three VMX copy kernels (no direct callers): r3 = dst (128-byte aligned, dcbzl), r4 = src, r5 = byte count;
// they advance r3, so it is read first. Unstamped until run mw2sp_018 (texture shortcut on) went wrong; file reads
// stamp since then too (page_writes::Enable).
// ponytail: stamps 128 bytes past each end for the head/tail the caller may store itself around the kernel; hook the
// caller instead if a "changed without a write stamp" line still names a range they wrote.
namespace {
template <void (*Original)(PPCContext&, uint8_t*)>
inline void StampedKernel(PPCContext& ctx, uint8_t* base) {
  const uint32_t dst = ctx.r3.u32;
  const uint32_t n = ctx.r5.u32;
  Original(ctx, base);
  const uint32_t pad = (dst & 0x1FFFFFFFu) >= 128 ? 128 : 0;  // stay inside the view
  mw2mp::page_writes::MarkGuest(dst - pad, n + pad + 128);
}
}  // namespace

#if defined(MW2_CAMPAIGN) && MW2_CAMPAIGN
REX_EXTERN(__imp__sub_823712B4);
REX_EXTERN(__imp__sub_82371780);
REX_EXTERN(__imp__sub_82371B10);
REX_HOOK_RAW(sub_823712B4) { StampedKernel<__imp__sub_823712B4>(ctx, base); }
REX_HOOK_RAW(sub_82371780) { StampedKernel<__imp__sub_82371780>(ctx, base); }
REX_HOOK_RAW(sub_82371B10) { StampedKernel<__imp__sub_82371B10>(ctx, base); }
#else
REX_EXTERN(__imp__sub_823A9D04);
REX_EXTERN(__imp__sub_823AA1D0);
REX_EXTERN(__imp__sub_823AA560);
REX_HOOK_RAW(sub_823A9D04) { StampedKernel<__imp__sub_823A9D04>(ctx, base); }
REX_HOOK_RAW(sub_823AA1D0) { StampedKernel<__imp__sub_823AA1D0>(ctx, base); }
REX_HOOK_RAW(sub_823AA560) { StampedKernel<__imp__sub_823AA560>(ctx, base); }
#endif

// The asset loader's read, DB_ReadXFileData (SP sub_82145608, MP twin sub_821B0B10): r3 = destination, r4 = bytes. It
// sets them as zlib's next_out / avail_out and inflates (SP sub_821446E8) straight into the destination with plain
// stores, so assets streamed in mid-level over memory that held others stamped nothing: the vertex guard caught
// 19940010 (+512K), 17900000 (+768K), 19A00000 and 1558000C minutes into runs mw2sp_048-054, one stale flash each.
#if defined(MW2_CAMPAIGN) && MW2_CAMPAIGN
#define MW2_DB_READ sub_82145608
#define MW2_DB_READ_IMP __imp__sub_82145608
#else
#define MW2_DB_READ sub_821B0B10
#define MW2_DB_READ_IMP __imp__sub_821B0B10
#endif
REX_EXTERN(MW2_DB_READ_IMP);
REX_HOOK_RAW(MW2_DB_READ) {
  const uint32_t dst = ctx.r3.u32;
  const uint32_t n = ctx.r4.u32;
  MW2_DB_READ_IMP(ctx, base);
  mw2mp::page_writes::MarkGuest(dst, n);
}

// MW2's CPU skinning kernels write each skinned vertex (32 bytes) with VMX stores and stamped nothing: run mw2sp_019's
// vertex cache guard caught 236 ranges changed without a stamp in the first 20 s of a mission, the stretch where NPC
// models were seen shifting (until the guard notices, the cache serves the previous skin). Read from their PowerPC code:
//   1-4 bones (SP 823D7178/7318/7530/7770, MP 82413980/13B20/13D38/13F78): r7 = output, r5 = vertex count;
//   rigid, one matrix (SP 823D79F0, MP 824141F8): r7 = output, r4 = count;
//   rigid, per-surface groups (SP 823D7AF0, MP 824142F8): r6 = output, r4 = total count (groups written back to back).
// mw2mp_native_skin.cpp (not compiled) hooks the 1-4 bone ones too: stamp there if it comes back.
namespace {
template <void (*Original)(PPCContext&, uint8_t*), int kOut, int kCount>
inline void StampedSkin(PPCContext& ctx, uint8_t* base) {
  const uint32_t out = kOut == 7 ? ctx.r7.u32 : ctx.r6.u32;
  const uint32_t count = kCount == 5 ? ctx.r5.u32 : ctx.r4.u32;
  Original(ctx, base);
  mw2mp::page_writes::MarkGuest(out, count * 32u);
}
}  // namespace

#if defined(MW2_CAMPAIGN) && MW2_CAMPAIGN
REX_EXTERN(__imp__sub_823D7178);
REX_EXTERN(__imp__sub_823D7318);
REX_EXTERN(__imp__sub_823D7530);
REX_EXTERN(__imp__sub_823D7770);
REX_EXTERN(__imp__sub_823D79F0);
REX_EXTERN(__imp__sub_823D7AF0);
REX_HOOK_RAW(sub_823D7178) { StampedSkin<__imp__sub_823D7178, 7, 5>(ctx, base); }
REX_HOOK_RAW(sub_823D7318) { StampedSkin<__imp__sub_823D7318, 7, 5>(ctx, base); }
REX_HOOK_RAW(sub_823D7530) { StampedSkin<__imp__sub_823D7530, 7, 5>(ctx, base); }
REX_HOOK_RAW(sub_823D7770) { StampedSkin<__imp__sub_823D7770, 7, 5>(ctx, base); }
REX_HOOK_RAW(sub_823D79F0) { StampedSkin<__imp__sub_823D79F0, 7, 4>(ctx, base); }
REX_HOOK_RAW(sub_823D7AF0) { StampedSkin<__imp__sub_823D7AF0, 6, 4>(ctx, base); }
#else
REX_EXTERN(__imp__sub_82413980);
REX_EXTERN(__imp__sub_82413B20);
REX_EXTERN(__imp__sub_82413D38);
REX_EXTERN(__imp__sub_82413F78);
REX_EXTERN(__imp__sub_824141F8);
REX_EXTERN(__imp__sub_824142F8);
REX_HOOK_RAW(sub_82413980) { StampedSkin<__imp__sub_82413980, 7, 5>(ctx, base); }
REX_HOOK_RAW(sub_82413B20) { StampedSkin<__imp__sub_82413B20, 7, 5>(ctx, base); }
REX_HOOK_RAW(sub_82413D38) { StampedSkin<__imp__sub_82413D38, 7, 5>(ctx, base); }
REX_HOOK_RAW(sub_82413F78) { StampedSkin<__imp__sub_82413F78, 7, 5>(ctx, base); }
REX_HOOK_RAW(sub_824141F8) { StampedSkin<__imp__sub_824141F8, 7, 4>(ctx, base); }
REX_HOOK_RAW(sub_824142F8) { StampedSkin<__imp__sub_824142F8, 6, 4>(ctx, base); }
#endif

// The effects' code mesh: every FX writer reserves vertices and indices with SP sub_82394CB8 (MP twin sub_823D17F0,
// same instructions; r3 = the buffer: +4 vertices used, +8 indices used, +16 vertex base (32-byte vertices), +20 index
// base (16-bit), +40 / +44 their capacities), then stores into them with plain stores through sub_82394D70 (base +
// index x 32) from a dozen functions (sub_82185938, sub_821839D0, ...; store watch run mw2sp_051: 1DA80000-1DD00000).
// The buffers are rewritten every frame, so the first reserve on each one marks it whole as written every frame.
// Without this the vertex guard had to catch each 256 KB of it first: run mw2sp_052, a chunk the guard never sampled
// drew pale stretched triangles at invasion's Nate's for as long as it stayed; run 053, a flash until 1DAC0000 was caught.
#if defined(MW2_CAMPAIGN) && MW2_CAMPAIGN
#define MW2_CODE_MESH_RESERVE sub_82394CB8
#define MW2_CODE_MESH_RESERVE_IMP __imp__sub_82394CB8
#else
#define MW2_CODE_MESH_RESERVE sub_823D17F0
#define MW2_CODE_MESH_RESERVE_IMP __imp__sub_823D17F0
#endif
REX_EXTERN(MW2_CODE_MESH_RESERVE_IMP);
namespace {
std::atomic<uint32_t> g_code_mesh_seen[8] = {};  // vertex bases already marked (two frames' buffers, with room)
std::atomic<uint32_t> g_frame_mesh_seen[8] = {};

uint32_t CodeMeshPhysical(uint32_t guest) {
  return (guest & 0x1FFFFFFFu) + (guest >= 0xE0000000u ? 0x1000u : 0u);  // as page_writes::MarkGuest
}

// Marks a per-frame vertex + index buffer pair as written every frame the first time its vertex base shows up.
// Volatile marks are permanent, so once per base is enough; bases past the 8 slots are left to the guard.
void MarkMeshOnce(std::atomic<uint32_t> (&seen)[8], const char* what, uint32_t vertices, uint32_t vertex_bytes,
                  uint32_t indices, uint32_t index_bytes) {
  for (auto& slot : seen) {
    const uint32_t s = slot.load(std::memory_order_relaxed);
    if (s == vertices) {
      return;
    }
    if (s != 0) {
      continue;
    }
    if (vertices < 0xA0000000u || indices < 0xA0000000u || vertex_bytes > (64u << 20) || index_bytes > (64u << 20)) {
      return;  // not physical memory or nonsense sizes: left to the guard
    }
    uint32_t expected = 0;
    if (!slot.compare_exchange_strong(expected, vertices, std::memory_order_relaxed) && expected == vertices) {
      return;  // another thread took this slot for the same buffer
    }
    mw2mp::page_writes::MarkVolatile(CodeMeshPhysical(vertices), vertex_bytes);
    mw2mp::page_writes::MarkVolatile(CodeMeshPhysical(indices), index_bytes);
    REXLOG_INFO("[native] C6 {} at {:08X}+{} (vertices) and {:08X}+{} (indices): marked as written every frame", what,
                CodeMeshPhysical(vertices), vertex_bytes, CodeMeshPhysical(indices), index_bytes);
    return;
  }
}
}  // namespace
REX_HOOK_RAW(MW2_CODE_MESH_RESERVE) {
  const uint32_t buffer = ctx.r3.u32;
  MW2_CODE_MESH_RESERVE_IMP(ctx, base);
  if (!mw2mp::page_writes::g_enabled.load(std::memory_order_relaxed)) {
    return;
  }
  const uint8_t* const b = Host(base, buffer);
  MarkMeshOnce(g_code_mesh_seen, "FX code mesh", rex::memory::load_and_swap<uint32_t>(b + 16),
               rex::memory::load_and_swap<uint32_t>(b + 40) * 32u, rex::memory::load_and_swap<uint32_t>(b + 20),
               rex::memory::load_and_swap<uint32_t>(b + 44) * 2u);
}

// The frame's second dynamic mesh (store watch run mw2sp_051: 1DA40000 / 1DC00000, written by sub_82193B60 /
// sub_82193F00 through sub_823B8C70 = base + index x 32). SP sub_823B8B90 (MP twin sub_823F1FD0, same instructions)
// starts it each frame: the state at SP 0x83E39200 (MP 0x83D7D600) gets +4 vertices used, +8 indices used, +12 vertex
// base (32-byte vertices), +16 index base (16-bit) from the frame context. Reserves cap them at 8448 vertices
// (sub_823B8CC8 / MP sub_823F2108) and 18432 indices (sub_823B8C30 / MP sub_823F2070). Run mw2sp_054: the vertex guard
// still caught 1DA40000 / 1DC00000 once each at level start (a flash of up to 16 frames).
#if defined(MW2_CAMPAIGN) && MW2_CAMPAIGN
#define MW2_FRAME_MESH_BEGIN sub_823B8B90
#define MW2_FRAME_MESH_BEGIN_IMP __imp__sub_823B8B90
constexpr uint32_t kFrameMeshState = 0x83E39200u;
#else
#define MW2_FRAME_MESH_BEGIN sub_823F1FD0
#define MW2_FRAME_MESH_BEGIN_IMP __imp__sub_823F1FD0
constexpr uint32_t kFrameMeshState = 0x83D7D600u;
#endif
REX_EXTERN(MW2_FRAME_MESH_BEGIN_IMP);
REX_HOOK_RAW(MW2_FRAME_MESH_BEGIN) {
  MW2_FRAME_MESH_BEGIN_IMP(ctx, base);
  if (!mw2mp::page_writes::g_enabled.load(std::memory_order_relaxed)) {
    return;
  }
  const uint8_t* const s = Host(base, kFrameMeshState);
  MarkMeshOnce(g_frame_mesh_seen, "frame mesh", rex::memory::load_and_swap<uint32_t>(s + 12), 8448u * 32u,
               rex::memory::load_and_swap<uint32_t>(s + 16), 18432u * 2u);
}

#if defined(MW2_CAMPAIGN) && MW2_CAMPAIGN
// The campaign's job-queue scan, sub_823B7430 (the worker loop sub_823B79C0 calls it after each 1 ms wait on the work
// event; sub_823B7270 too). Run mw2sp_014: Worker2 spent 49 % of its busy time in this function's own loop. Each of the
// 28 queues it visits ends with lwarx/stwcx. on the shared "lowest queue with work" word (0x83E28A80): when the word is
// not this queue the stwcx. writes back the value just loaded. On the PPC that is a plain reservation clear with no
// barrier; recompiled, it is a full atomic compare-and-swap on a line the producers keep writing, so the scan
// ping-pongs that line 28 times a pass. This is the same function without that no-op store: every load, every job run
// (sub_823B6D70), every real hint clear (stwcx. of 0x7FFFFFFF), the ResetEvent (sub_8228BD78) and the order are the
// game's. mw2mp_native_job_scan false = the recompiled one.
REXCVAR_DEFINE_BOOL(mw2mp_native_job_scan, true, "MW2",
                    "Campaign: the job-queue scan (sub_823B7430) runs natively, without the recompiled code's atomic "
                    "write-back of the queue hint on every queue it visits. false = the recompiled scan");
REX_EXTERN(__imp__sub_823B7430);
REX_EXTERN(__imp__sub_823B6D70);  // call only
REX_EXTERN(__imp__sub_8228BD78);  // call only
// The worker loop's wait (sub_8228BD30 = WaitForSingleObject(work event, 1 ms)) backs off while the scans find nothing.
// sub_823B79C0 (Worker2's loop) bumps the waiting-workers count at 0x83E28A88, waits, drops it, scans. Producers
// (sub_823B6AA8, sub_823B6B08) lower the queue hint, then set the event only if that count is nonzero; the 1 ms timeout
// covers a push that lands between a scan's last pass and the count going up. Run mw2sp_037: Worker2 17 % CPU, about
// 10 % of a core in the scans of those wakes (1,000 a second, most finding nothing). Backed off, a worker woken by the
// event wakes at once as before; only that narrow race can wait longer (up to the maximum). Main's job waits
// (sub_823B7840) and the other worker loop (sub_823B7920) keep the game's 1 ms.
REXCVAR_DEFINE_INT32(mw2mp_job_wait_max_ms, 4, "MW2",
                     "Campaign: Worker2's longest wait for the job event between scans. 1 ms after a scan that ran a "
                     "job, doubling after each scan that ran none, up to this. 1 = the game's 1 ms poll (1-16)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REX_EXTERN(__imp__sub_8228BD30);
REX_EXTERN(__imp__sub_8236FB78);  // call only
namespace {
thread_local bool t_job_ran = false;    // the native scan ran a job since this thread's last wait
thread_local uint32_t t_job_wait_ms = 1;
constexpr uint32_t kJobPreferred = 0x8241F580u;  // lis r10,-32190; addi r26,r10,-2688: s32, the preferred queue
constexpr uint32_t kJobEnabled = kJobPreferred + 120;  // byte per queue
constexpr uint32_t kJobHint = 0x83E28A80u;      // lis r11,-31773; addi r29,r11,-30080
constexpr uint32_t kJobCounts = kJobHint - 11776 + 16;  // s32 per queue, 128 bytes apart
constexpr int32_t kJobQueues = 28;              // (3600 - 16) / 128
constexpr uint32_t kJobHintNone = 0x7FFFFFFFu;

inline uint32_t GuestLoad(uint8_t* base, uint32_t guest) {
  return __builtin_bswap32(*reinterpret_cast<volatile uint32_t*>(Host(base, guest)));
}
inline bool JobQueueReady(uint8_t* base, int32_t q) {
  return *reinterpret_cast<volatile uint8_t*>(Host(base, kJobEnabled + uint32_t(q))) != 0;
}
inline int32_t JobCount(uint8_t* base, int32_t q) { return int32_t(GuestLoad(base, kJobCounts + uint32_t(q) * 128)); }
// sub_823B6D70(q, 0): runs one job of queue q; nonzero when it ran one.
inline int32_t RunJob(PPCContext& ctx, uint8_t* base, int32_t q, uint32_t lr) {
  ctx.r4.s64 = 0;
  ctx.r3.u64 = uint32_t(q);
  ctx.lr = lr;
  __imp__sub_823B6D70(ctx, base);  // call only
  t_job_ran |= ctx.r3.s32 != 0;
  return ctx.r3.s32;
}
// The game's lwarx/cmpw/stwcx. loop: the hint goes to "none" if it still names q. The no-match path's stwcx. of the
// same value is the one left out.
inline void ClearJobHint(uint8_t* base, int32_t q) {
  uint32_t* const word = reinterpret_cast<uint32_t*>(Host(base, kJobHint));
  for (;;) {
    const uint32_t raw = __atomic_load_n(word, __ATOMIC_RELAXED);
    if (int32_t(__builtin_bswap32(raw)) != q ||
        __sync_bool_compare_and_swap(word, raw, __builtin_bswap32(kJobHintNone))) {
      return;
    }
  }
}
}  // namespace

REX_HOOK_RAW(sub_823B7430) {
  if (!REXCVAR_GET(mw2mp_native_job_scan)) {
    __imp__sub_823B7430(ctx, base);
    return;
  }
  // stwu r1,-144(r1) (the frame the callees run under) and the matching addi on the way out.
  const uint32_t sp = ctx.r1.u32;
  *reinterpret_cast<volatile uint32_t*>(Host(base, sp - 144)) = __builtin_bswap32(sp);
  ctx.r1.u32 = sp - 144;
  bool reset_left = true;  // r25: one ResetEvent and one more pass before returning
  for (;;) {
    // loc_823B7458: the preferred queue first.
    int32_t q = int32_t(GuestLoad(base, kJobPreferred));
    if (q >= 0 && JobQueueReady(base, q) && JobCount(base, q) > 0 && RunJob(ctx, base, q, 0x823B749C) != 0) {
      while (RunJob(ctx, base, q, 0x823B75BC) != 0) {  // loc_823B75B0
      }
      ClearJobHint(base, q);
      continue;
    }
    // loc_823B74A4: from the hinted queue (0 with no hint) to the last.
    const uint32_t hint = GuestLoad(base, kJobHint);
    const int32_t start = hint == kJobHintNone ? 0 : int32_t(hint);
    bool ran = false;
    for (q = start; q >= 0 && q < kJobQueues; ++q) {
      if (!JobQueueReady(base, q)) {
        continue;
      }
      if (JobCount(base, q) > 0 && RunJob(ctx, base, q, 0x823B74FC) != 0) {
        // loc_823B7568: this queue while the hint is not below it.
        while (int32_t(GuestLoad(base, kJobHint)) >= q) {
          if (RunJob(ctx, base, q, 0x823B7580) == 0) {
            ClearJobHint(base, q);
            break;
          }
        }
        ran = true;
        break;
      }
      ClearJobHint(base, q);
    }
    if (ran || start != 0) {
      continue;
    }
    if (!reset_left) {
      break;
    }
    ctx.lr = 0x823B7560;
    __imp__sub_8228BD78(ctx, base);  // ResetEvent of the work event, then one more pass; call only
    reset_left = false;
  }
  ctx.r1.u32 = sp;
}

REX_HOOK_RAW(sub_8228BD30) {
  static const uint32_t max_ms = uint32_t(std::clamp(REXCVAR_GET(mw2mp_job_wait_max_ms), 1, 16));
  // Only from sub_823B79C0's wait (return address 0x823B7A14), and only with the native scan (it reports the jobs).
  if (ctx.lr != 0x823B7A14 || max_ms <= 1 || !REXCVAR_GET(mw2mp_native_job_scan)) {
    __imp__sub_8228BD30(ctx, base);
    return;
  }
  t_job_wait_ms = t_job_ran ? 1 : std::min(t_job_wait_ms * 2, max_ms);
  t_job_ran = false;
  // The game's body with r4 = the wait: stwu r1,-96(r1); r3 = the work event (lwz r3,-16320 off 0x83510000);
  // WaitForSingleObject; r3 = 1 when it was signalled (cntlzw / rlwinm), the caller ignores it.
  const uint32_t sp = ctx.r1.u32;
  *reinterpret_cast<volatile uint32_t*>(Host(base, sp - 96)) = __builtin_bswap32(sp);
  ctx.r1.u32 = sp - 96;
  ctx.r3.u64 = GuestLoad(base, 0x8350C040u);
  ctx.r4.u64 = t_job_wait_ms;
  const uint64_t lr = ctx.lr;
  ctx.lr = 0x8228BD4C;
  __imp__sub_8236FB78(ctx, base);  // call only
  ctx.lr = lr;
  ctx.r3.u64 = ctx.r3.u32 == 0 ? 1 : 0;
  ctx.r1.u32 = sp;
}
#endif

// The streaming allocator's retry sleep. The Database thread's loader (SP sub_82178838, MP twin with the same
// instructions) tries an allocation; when it fails it calls sub_823A5548(0) (MP sub_823DE988) and Sleep(50), then tries
// again. Run mw2sp_021 (NSP, dcburning): in the hitches Main waited 18-40 ms on a critical section held by Database
// (82179E34) while Database slept 50 ms at this call. The same loop with a shorter sleep retries sooner; the game's
// order of calls is unchanged. Only that call site: the wrapper is every Sleep of the game.
REXCVAR_DEFINE_INT32(mw2mp_stream_retry_sleep_ms, 5, "MW2",
                     "The streaming loader's sleep after a failed allocation, in ms (the game's is 50)")
    .range(1, 50)
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
#if defined(MW2_CAMPAIGN) && MW2_CAMPAIGN
#define MW2_SLEEP_WRAPPER sub_8228B0E0
#define MW2_SLEEP_WRAPPER_IMP __imp__sub_8228B0E0
constexpr uint32_t kStreamRetrySleepSite = 0x82178A2Cu;  // li r3,50; bl 0x8228b0e0 (return address)
#else
#define MW2_SLEEP_WRAPPER sub_82292558
#define MW2_SLEEP_WRAPPER_IMP __imp__sub_82292558
constexpr uint32_t kStreamRetrySleepSite = 0x821E3FFCu;  // li r3,50; bl 0x82292558 (return address)
#endif
REX_EXTERN(MW2_SLEEP_WRAPPER_IMP);
REX_HOOK_RAW(MW2_SLEEP_WRAPPER) {
  if (uint32_t(ctx.lr) == kStreamRetrySleepSite && ctx.r3.u32 == 50) {
    static const int32_t ms = REXCVAR_GET(mw2mp_stream_retry_sleep_ms);
    ctx.r3.u64 = uint32_t(ms);
  }
  MW2_SLEEP_WRAPPER_IMP(ctx, base);
}
