// A native version of the game's bounds-tree frustum culling, verified against the original in a checked phase
// before it runs on its own. Port of bo2-360-nx's bo2zm_native_cull.cpp (2.0x per call there) to MW2's simpler tree
// walk (no occluders).
//
// What the game does
// ------------------
// Campaign sub_823D1598 / multiplayer sub_8240DDA0 (same 250 instructions, only the addresses differ), called by
// sub_823D19E8 / its twin with r3 = the root node and r4 = a plane list:
//   - Plane list: up to 16 plane pointers at +0, the count at +64. Each plane: the plane (aligned 16 bytes) and a
//     sign vector at +16 (+-1 per axis, the caller's fsel), so mid +- half * sign picks the box's far/near corner.
//   - Node: box midpoint (unaligned 16 bytes at +0) and half size (+12), both ANDed with kConstMask (drops w) and the
//     midpoint given w = 1 by adding kConstW; child count u16 at +24, children at node + [node + 36], 40 bytes each;
//     items in two lists: A (count +30, u16 indices at [node + 32]) and B (count +26, u16 indices from
//     [world + 72] + [node + 28] * 2; world = [kWorld]).
// For a node it:
//   1. per plane: lo = dot(plane, mid - half * sign), hi = dot(plane, mid + half * sign); the plane is kept (in its
//      frame at r1 + 96, the count at r1 + 160: the same layout as a plane list) when kConstKeep > lo; min of hi over
//      the planes, starting from kConstMin;
//   2. returns when that minimum is not above the float at kOutside (outside a plane);
//   3. no plane kept: every item of both lists is visible;
//   4. else, with children: each child with the kept planes;
//   5. else, each item not yet visible whose box (A at [world + 76] + index * 36, B at [world + 84] + index * 32: the
//      same midpoint/half-size pair) has min over the kept planes of dot(plane, mid + half * sign) above kConstKeep.
// Visible = byte 1 in two arrays whose pointers are in the thread's block ([r13 + 0] + kArrayA / kArrayB), shared
// by the workers culling other parts of the tree. It is a void function, but r3 travels back through two tail calls
// (sub_823D19E8, sub_823D1AB0), so the native version leaves the same r3: the node after steps 2 and 3, the last
// child's r3 after step 4, the B array pointer after step 5 when list B has items (lwzx r3), else the node.
//
// Natively: the same simde operations the generated code performs (mul then add for vmaddfp, dp_ps 0xFF, min_ps,
// cmpgt_ps), on the same byte order, with flush-to-zero switched as it switches it. The guard, as bo2: a checked call
// points the thread's two array pointers at a private zeroed pair, runs the game's function, keeps the arrays,
// zeroes them again and runs the native version on the same input; arrays and r3 must be equal. Then the pointers are
// put back and the game's function runs for real. The first kChecked top-level calls are checked, then one in
// kCheckEvery; a difference logs a MISMATCH and turns the native version off for the rest of the run. Items' boxes
// are scattered through the world's arrays, so the leaf loops prefetch kPrefetchAhead items ahead (bo2: a third of
// the item test's samples waited on that load).
//
// Profile (mp run 040): this loop took 3.5 % of Main + Backend.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <vector>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>
#include <rex/ppc/intrinsics.h>  // simde and the generated code's byte order table (VectorMaskL)
#include <rex/runtime.h>
#include <rex/system/xmemory.h>

REXCVAR_DEFINE_BOOL(mw2mp_native_cull, true, "MW2",
                    "The game's bounds-tree frustum culling in native code, checked against the game's own first "
                    "(\"[native_cull]\" lines). false = the game's code only")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

#if defined(MW2_CAMPAIGN) && MW2_CAMPAIGN
#define MW2_CULL_FN sub_823D1598
REX_EXTERN(__imp__sub_823D1598);
#else
#define MW2_CULL_FN sub_8240DDA0
REX_EXTERN(__imp__sub_8240DDA0);
#endif
#define MW2_CULL_CAT2(a, b) a##b
#define MW2_CULL_CAT(a, b) MW2_CULL_CAT2(a, b)
#define MW2_CULL_GAME MW2_CULL_CAT(__imp__, MW2_CULL_FN)

namespace {

using Clock = std::chrono::steady_clock;
using VI = simde__m128i;
using V = simde__m128;

#if defined(MW2_CAMPAIGN) && MW2_CAMPAIGN
constexpr uint32_t kWorld = 0x83C93464;      // [kWorld] = the world (lis r10,-31799; lwz r10,13412(r10))
constexpr uint32_t kConstMask = 0x83938980;  // v62: ANDed with the boxes
constexpr uint32_t kConstOne = 0x839387B0;   // v61 in the plane loop: ANDed with the "kept" compare
constexpr uint32_t kConstMin = 0x8203E980;   // v59: the minimum's start
constexpr uint32_t kConstW = 0x8203E970;     // v60: added to the midpoint
constexpr uint32_t kConstKeep = 0x8203E960;  // v58
constexpr uint32_t kOutside = 0x82003994;    // float: the minimum must be above it
constexpr uint32_t kArrayA = 32;             // offsets in the thread's block ([r13 + 0])
constexpr uint32_t kArrayB = 36;
#else
constexpr uint32_t kWorld = 0x83B3CBE4;
constexpr uint32_t kConstMask = 0x8379B860;
constexpr uint32_t kConstOne = 0x8379B610;
constexpr uint32_t kConstMin = 0x820551B0;
constexpr uint32_t kConstW = 0x820551A0;
constexpr uint32_t kConstKeep = 0x82055190;
constexpr uint32_t kOutside = 0x820015A4;
constexpr uint32_t kArrayA = 44;
constexpr uint32_t kArrayB = 48;
#endif
constexpr uint32_t kMaxPlanes = 16;        // the frame holds 16 plane pointers (r1 + 96 .. r1 + 160)
constexpr uint32_t kArrayBytes = 0x10000;  // 16-bit item indices
constexpr uint64_t kChecked = 2000;
constexpr uint64_t kCheckEvery = 1024;
constexpr uint32_t kPrefetchAhead = 4;

enum Mode : int { kNone, kOriginal };
thread_local int t_mode = kNone;  // inside a checked call's game run: its recursion is the game's too

std::atomic<int> g_phase{0};  // 0 checking, 1 native alone, 2 off
std::atomic<uint64_t> g_calls{0};
std::atomic<uint64_t> g_checked{0};
std::atomic<uint64_t> g_ns_original{0};
std::atomic<uint64_t> g_ns_native{0};
std::atomic<uint64_t> g_timed{0};
std::atomic<int64_t> g_last_report{0};
std::mutex g_check_mutex;  // the scratch arrays are shared by the checks
uint32_t g_scratch = 0;

inline uint32_t Read32(const uint8_t* base, uint32_t a) {
  uint32_t v;
  std::memcpy(&v, base + a, 4);
  return __builtin_bswap32(v);
}
inline uint16_t Read16(const uint8_t* base, uint32_t a) {
  uint16_t v;
  std::memcpy(&v, base + a, 2);
  return __builtin_bswap16(v);
}
inline void Write32(uint8_t* base, uint32_t a, uint32_t v) {
  v = __builtin_bswap32(v);
  std::memcpy(base + a, &v, 4);
}
inline uint64_t NsSince(Clock::time_point t) {
  return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - t).count());
}

inline VI Reverse() { return simde_mm_loadu_si128(reinterpret_cast<const VI*>(rex::ppc::VectorMaskL)); }
// lvx128: the aligned 16 bytes, in the generated code's register byte order.
inline VI LoadAligned(const uint8_t* base, uint32_t a) {
  return simde_mm_shuffle_epi8(simde_mm_load_si128(reinterpret_cast<const VI*>(base + (a & ~15u))), Reverse());
}
// lvlx128 a | lvrx128 a + 16: the 16 bytes at a, any alignment, in the same byte order.
inline VI LoadUnaligned(const uint8_t* base, uint32_t a) {
  return simde_mm_shuffle_epi8(simde_mm_loadu_si128(reinterpret_cast<const VI*>(base + a)), Reverse());
}
// vspltw128 v, 0 then stvewx128 and lwz: the guest element 0 as a word.
inline uint32_t Element0(VI v) { return uint32_t(simde_mm_cvtsi128_si32(simde_mm_shuffle_epi32(v, 0xFF))); }
inline float Element0F(V v) {
  const uint32_t w = Element0(simde_mm_castps_si128(v));
  float f;
  std::memcpy(&f, &w, 4);
  return f;
}
// vmaddfp d, a, b, c as the generated code computes it: add(mul(a, b), c).
inline V MulAdd(V a, V b, V c) { return simde_mm_add_ps(simde_mm_mul_ps(a, b), c); }

struct Plane {
  V plane;
  V sign;
};

struct Consts {
  VI mask;
  VI one;
  V min_start;
  V w;
  V keep;
  float outside;
};

struct Native {
  PPCContext& ctx;
  uint8_t* base;
  uint32_t world;
  uint32_t array_a;
  uint32_t array_b;
  Consts k;
};

inline void PrefetchItem(const uint8_t* base, uint32_t box, uint32_t visible) {
  __builtin_prefetch(base + box);
  __builtin_prefetch(base + box + 27);
  __builtin_prefetch(base + visible);
}

// Step 3: every item of both lists visible.
void MarkAll(Native& n, uint32_t node) {
  uint8_t* base = n.base;
  const uint32_t count_a = Read16(base, node + 30);
  if (count_a) {
    const uint32_t list = Read32(base, node + 32);
    for (uint32_t i = 0; i < count_a; ++i) {
      base[n.array_a + Read16(base, list + 2 * i)] = 1;
    }
  }
  const uint32_t count_b = Read16(base, node + 26);
  if (count_b) {
    const uint32_t list = Read32(base, n.world + 72) + uint32_t(Read16(base, node + 28)) * 2;
    for (uint32_t i = 0; i < count_b; ++i) {
      base[n.array_b + Read16(base, list + 2 * i)] = 1;
    }
  }
}

// One item of a leaf (step 5).
inline void Item(Native& n, uint32_t box, uint32_t array, uint32_t index, const Plane* planes, uint32_t count) {
  uint8_t* base = n.base;
  if (base[array + index] != 0) {
    return;
  }
  const V mid = simde_mm_castsi128_ps(simde_mm_and_si128(LoadUnaligned(base, box), n.k.mask));        // v40
  const V half = simde_mm_castsi128_ps(simde_mm_and_si128(LoadUnaligned(base, box + 12), n.k.mask));  // v13
  n.ctx.fpscr.enableFlushMode();
  const V mid1 = simde_mm_add_ps(mid, n.k.w);  // v12
  V minimum = n.k.min_start;
  for (uint32_t i = 0; i < count; ++i) {
    const V corner = MulAdd(half, planes[i].sign, mid1);
    minimum = simde_mm_min_ps(minimum, simde_mm_dp_ps(planes[i].plane, corner, 0xFF));
  }
  if (Element0(simde_mm_castps_si128(simde_mm_cmpgt_ps(minimum, n.k.keep))) != 0) {
    base[array + index] = 1;
  }
}

// One node (the game function's body); returns the r3 the game's leaves.
uint32_t Cull(Native& n, uint32_t node, const Plane* planes, uint32_t count) {
  uint8_t* base = n.base;
  PPCContext& ctx = n.ctx;
  // Step 1: the planes.
  const VI mid_raw = simde_mm_and_si128(LoadUnaligned(base, node), n.k.mask);        // v54
  const VI half_raw = simde_mm_and_si128(LoadUnaligned(base, node + 12), n.k.mask);  // v11
  const VI sign_bit = simde_mm_set1_epi32(int32_t(0x80000000u));                     // v55
  const V half = simde_mm_castsi128_ps(half_raw);
  const V neg_half = simde_mm_castsi128_ps(simde_mm_xor_si128(half_raw, sign_bit));  // v10
  ctx.fpscr.enableFlushMode();
  const V mid1 = simde_mm_add_ps(simde_mm_castsi128_ps(mid_raw), n.k.w);  // v13
  Plane kept[kMaxPlanes];
  uint32_t kept_count = 0;
  V minimum = n.k.min_start;
  for (uint32_t i = 0; i < count; ++i) {
    const V lo = simde_mm_dp_ps(planes[i].plane, MulAdd(neg_half, planes[i].sign, mid1), 0xFF);  // v52
    const V hi = simde_mm_dp_ps(planes[i].plane, MulAdd(half, planes[i].sign, mid1), 0xFF);      // v51
    if (kept_count >= kMaxPlanes) [[unlikely]] {
      return UINT32_MAX;  // only if kConstOne were not 1: give up, the game's function runs
    }
    kept[kept_count] = planes[i];  // stored at the kept count; kept only if the word below is not 0
    const VI cuts = simde_mm_and_si128(simde_mm_castps_si128(simde_mm_cmpgt_ps(n.k.keep, lo)), n.k.one);
    minimum = simde_mm_min_ps(minimum, hi);
    kept_count += Element0(cuts);
  }
  if (kept_count > kMaxPlanes) [[unlikely]] {
    return UINT32_MAX;
  }
  ctx.fpscr.disableFlushMode();
  if (!(double(Element0F(minimum)) > double(n.k.outside))) {
    return node;  // outside
  }
  if (kept_count == 0) {
    MarkAll(n, node);  // step 3
    return node;
  }
  const uint32_t children = Read16(base, node + 24);
  if (children != 0) {  // step 4
    uint32_t child = node + Read32(base, node + 36);
    uint32_t r3 = node;
    for (uint32_t i = 0; i < children; ++i, child += 40) {
      r3 = Cull(n, child, kept, kept_count);
      if (r3 == UINT32_MAX) [[unlikely]] {
        return r3;
      }
    }
    return r3;
  }
  // Step 5: the items of the leaf.
  const uint32_t count_a = Read16(base, node + 30);
  if (count_a) {
    const uint32_t boxes = Read32(base, n.world + 76);
    const uint32_t list = Read32(base, node + 32);
    for (uint32_t i = 0; i < count_a && i < kPrefetchAhead; ++i) {
      const uint32_t index = Read16(base, list + 2 * i);
      PrefetchItem(base, boxes + index * 36, n.array_a + index);
    }
    for (uint32_t i = 0; i < count_a; ++i) {
      if (i + kPrefetchAhead < count_a) {
        const uint32_t ahead = Read16(base, list + 2 * (i + kPrefetchAhead));
        PrefetchItem(base, boxes + ahead * 36, n.array_a + ahead);
      }
      const uint32_t index = Read16(base, list + 2 * i);
      Item(n, boxes + index * 36, n.array_a, index, kept, kept_count);
    }
  }
  const uint32_t count_b = Read16(base, node + 26);
  if (count_b == 0) {
    return node;
  }
  const uint32_t list = Read32(base, n.world + 72) + uint32_t(Read16(base, node + 28)) * 2;
  const uint32_t boxes = Read32(base, n.world + 84);
  for (uint32_t i = 0; i < count_b && i < kPrefetchAhead; ++i) {
    const uint32_t index = Read16(base, list + 2 * i);
    PrefetchItem(base, boxes + index * 32, n.array_b + index);
  }
  for (uint32_t i = 0; i < count_b; ++i) {
    if (i + kPrefetchAhead < count_b) {
      const uint32_t ahead = Read16(base, list + 2 * (i + kPrefetchAhead));
      PrefetchItem(base, boxes + ahead * 32, n.array_b + ahead);
    }
    const uint32_t index = Read16(base, list + 2 * i);
    Item(n, boxes + index * 32, n.array_b, index, kept, kept_count);
  }
  return n.array_b;  // lwzx r3,r7,r8
}

// The whole function natively; false when it must give up (the game's function then runs).
bool NativeTop(PPCContext& ctx, uint8_t* base) {
  const uint32_t planes_at = ctx.r4.u32;
  const uint32_t count = Read32(base, planes_at + 64);
  if (count > kMaxPlanes) {
    return false;
  }
  const uint32_t block = Read32(base, ctx.r13.u32);
  Native n{ctx, base, Read32(base, kWorld), Read32(base, block + kArrayA), Read32(base, block + kArrayB), {}};
  n.k.mask = LoadAligned(base, kConstMask);
  n.k.one = LoadAligned(base, kConstOne);
  n.k.min_start = simde_mm_castsi128_ps(LoadAligned(base, kConstMin));
  n.k.w = simde_mm_castsi128_ps(LoadAligned(base, kConstW));
  n.k.keep = simde_mm_castsi128_ps(LoadAligned(base, kConstKeep));
  const uint32_t outside = Read32(base, kOutside);
  std::memcpy(&n.k.outside, &outside, 4);
  Plane planes[kMaxPlanes];
  for (uint32_t i = 0; i < count; ++i) {
    const uint32_t p = Read32(base, planes_at + 4 * i);
    planes[i].plane = simde_mm_castsi128_ps(LoadAligned(base, p));
    planes[i].sign = simde_mm_castsi128_ps(LoadAligned(base, p + 16));
  }
  const uint32_t r3 = Cull(n, ctx.r3.u32, planes, count);
  if (r3 == UINT32_MAX) [[unlikely]] {
    return false;  // nothing written that the game's run would not write the same
  }
  ctx.r3.u64 = r3;
  return true;
}

void Report() {
  const int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();
  int64_t last = g_last_report.load(std::memory_order_relaxed);
  if (last != 0 && now - last < 10000) {
    return;
  }
  if (!g_last_report.compare_exchange_strong(last, now, std::memory_order_relaxed) || last == 0) {
    return;
  }
  const uint64_t n = g_timed.load(std::memory_order_relaxed);
  const double us_original = n ? double(g_ns_original.load(std::memory_order_relaxed)) / double(n) / 1e3 : 0.0;
  const double us_native = n ? double(g_ns_native.load(std::memory_order_relaxed)) / double(n) / 1e3 : 0.0;
  const int phase = g_phase.load(std::memory_order_relaxed);
  REXLOG_INFO("[native_cull] top-level calls {} ({} checked, all identical so far), phase {}; per checked call "
              "{:.2f} us the game's, {:.2f} us native ({:.1f}x)",
              g_calls.load(std::memory_order_relaxed), g_checked.load(std::memory_order_relaxed),
              phase == 0 ? "checking" : phase == 1 ? "native" : "OFF", us_original, us_native,
              us_native > 0 ? us_original / us_native : 0.0);
}

void RunOriginal(PPCContext& ctx, uint8_t* base) {
  t_mode = kOriginal;
  MW2_CULL_GAME(ctx, base);
  t_mode = kNone;
}

// One checked top-level call (see the header comment).
void Checked(PPCContext& ctx, uint8_t* base) {
  std::lock_guard lock(g_check_mutex);
  if (g_scratch == 0) {
    g_scratch = rex::Runtime::instance()->memory()->SystemHeapAlloc(2 * kArrayBytes);
    if (g_scratch == 0) {
      REXLOG_ERROR("[native_cull] no guest memory for the check arrays: the native version is off");
      g_phase.store(2, std::memory_order_relaxed);
      RunOriginal(ctx, base);
      return;
    }
  }
  const uint32_t block = Read32(base, ctx.r13.u32);
  const uint32_t real_a = Read32(base, block + kArrayA);
  const uint32_t real_b = Read32(base, block + kArrayB);
  const PPCContext input = ctx;
  std::memset(base + g_scratch, 0, 2 * kArrayBytes);
  Write32(base, block + kArrayA, g_scratch);
  Write32(base, block + kArrayB, g_scratch + kArrayBytes);
  const auto t0 = Clock::now();
  RunOriginal(ctx, base);
  const uint64_t ns_original = NsSince(t0);
  const uint32_t r3_original = ctx.r3.u32;
  static std::vector<uint8_t> marks_original;  // under g_check_mutex
  marks_original.assign(base + g_scratch, base + g_scratch + 2 * kArrayBytes);
  ctx = input;
  ctx.fpscr.setcsr(ctx.fpscr.csr);
  std::memset(base + g_scratch, 0, 2 * kArrayBytes);
  const auto t1 = Clock::now();
  const bool native = NativeTop(ctx, base);
  const uint64_t ns_native = NsSince(t1);
  const uint32_t r3_native = ctx.r3.u32;
  Write32(base, block + kArrayA, real_a);
  Write32(base, block + kArrayB, real_b);
  if (native) {
    // r3 after step 5 is the B array pointer: the scratch one in both runs here.
    const bool same = r3_native == r3_original &&
                      std::memcmp(marks_original.data(), base + g_scratch, 2 * kArrayBytes) == 0;
    if (!same) {
      g_phase.store(2, std::memory_order_relaxed);
      uint32_t at = 0;
      while (at < 2 * kArrayBytes && marks_original[at] == base[g_scratch + at]) {
        ++at;
      }
      REXLOG_ERROR("[native_cull] MISMATCH after {} checked calls: {} (r3 game {:08X} native {:08X}; array byte {} "
                   "of {}: game {} native {}); node {:08X} planes {:08X}. The native version is off for the session",
                   g_checked.load(std::memory_order_relaxed),
                   r3_native != r3_original ? "r3 differs" : "the visible items differ", r3_original, r3_native, at,
                   2 * kArrayBytes, at < 2 * kArrayBytes ? marks_original[at] : 0,
                   at < 2 * kArrayBytes ? base[g_scratch + at] : 0, input.r3.u32, input.r4.u32);
    } else {
      g_ns_original.fetch_add(ns_original, std::memory_order_relaxed);
      g_ns_native.fetch_add(ns_native, std::memory_order_relaxed);
      g_timed.fetch_add(1, std::memory_order_relaxed);
      const uint64_t n = g_checked.fetch_add(1, std::memory_order_relaxed) + 1;
      int expected = 0;
      if (n >= kChecked && g_phase.compare_exchange_strong(expected, 1)) {
        REXLOG_INFO("[native_cull] {} checked calls identical: the native version runs alone now (1 in {} still "
                    "checked)",
                    n, kCheckEvery);
      }
    }
  }
  // For real: the game's function on the view's arrays.
  ctx = input;
  ctx.fpscr.setcsr(ctx.fpscr.csr);
  RunOriginal(ctx, base);
}

}  // namespace

REX_HOOK_RAW(MW2_CULL_FN) {
  if (t_mode == kOriginal) {  // recursion inside a game run
    MW2_CULL_GAME(ctx, base);
    return;
  }
  static const bool on = REXCVAR_GET(mw2mp_native_cull);
  const int phase = g_phase.load(std::memory_order_relaxed);
  if (!on || phase == 2) {
    MW2_CULL_GAME(ctx, base);
    return;
  }
  const uint64_t call = g_calls.fetch_add(1, std::memory_order_relaxed);
  if (phase == 1 && call % kCheckEvery != 0) {
    if (!NativeTop(ctx, base)) {
      RunOriginal(ctx, base);
    }
  } else {
    Checked(ctx, base);
  }
  if ((call & 1023) == 0) {
    Report();
  }
}
