// Native rewrites of the campaign's CPU vertex skinning kernels (TU6 default.xex): sub_823D7178 (one bone),
// sub_823D7318 (two), sub_823D7530 (three) and sub_823D7770 (four), as bo2-360-nx's bo2zm_native_skin.cpp does for BO2's three.
// The recompiled code runs every VMX128 instruction through the emulated register file (byte-reversing loads and
// stores, scalar 2:10:10:10 unpacks); here each vertex stays in NEON registers.
//
// What the four kernels do, read from their PowerPC code (r3 the vertices, 32 bytes each; r4 the bone records; r5 the
// count; r6 the bone matrices, 4 rows of 4 floats; r7 the output, 32 bytes each):
//   - record: bone 0's byte offset from r6 (u16), then per further bone its offset and its weight (u16 each): 2, 6, 10
//     or 14 bytes. A weight is read with lvlx at its address, a vperm with the vector at 0x8203E960 under the control
//     at 0x83E49D40 (both filled at run time) and vcuxwfp128 ..., 16;
//   - position (vertex +0, four floats): per bone B(p) = x * row 0 + (y * row 1 + (z * row 2 + row 3)); one bone: A(p);
//     more: (1 - sum of the weights) * A(p) + the weighted others, accumulated in bone order (w1 * B1, then + w2 * B2,
//     + w3 * B3), the sum as (w1 + w2) + w3. Out +0: vperm of that and the original w (control at 0x839385F0);
//   - the words at +24 and +28 (2:10:10:10): unpacked (vupkd3d128 type 8), times K11 (0x8203EA30) plus K10
//     (0x8203EA40), rotated by bone A alone (z * row 2, + y * row 1, + x * row 0), times K8 (0x8203EA20) plus K9
//     (0x8203EA10), packed back (vpkd3d128 type 2) into the same words; out +16 and +20 are the vertex's own.
//
// vmaddfp is a fused multiply-add on the 360 and here (vfmaq_f32); the recompiled code's add(mul()) is fused by GCC
// only where it chooses, so results can differ in the last bits. The checked phase therefore compares with a tolerance
// (positions: 1e-4 absolute plus 1e-5 relative; packed fields: 2 units), and any difference beyond it turns the rewrite
// off for the session and logs it.

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/memory/utils.h>
#include <rex/ppc/context.h>

#if defined(MW2_CAMPAIGN) && MW2_CAMPAIGN && defined(__aarch64__)

#include <arm_neon.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include <fmt/format.h>

REXCVAR_DEFINE_BOOL(mw2mp_native_skin, true, "MW2",
                    "Campaign: the CPU vertex skinning kernels (1-4 bones) run as native NEON code instead of the "
                    "recompiled VMX code. Checks itself against the game's own kernel and turns off on a difference "
                    "(\"[native_skin]\" lines). false = the game's code")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

REX_EXTERN(__imp__sub_823D7178);
REX_EXTERN(__imp__sub_823D7318);
REX_EXTERN(__imp__sub_823D7530);
REX_EXTERN(__imp__sub_823D7770);

namespace {

using F4 = float32x4_t;

constexpr uint32_t kBias9 = 0x8203EA10, kScale8 = 0x8203EA20, kScale11 = 0x8203EA30, kBias10 = 0x8203EA40;
constexpr uint32_t kOne = 0x8203E980, kWeightFill = 0x8203E960, kWeightControl = 0x83E49D40,
                   kPositionControl = 0x839385F0;
constexpr uint64_t kCheckedFirst = 500;  // calls checked from the start, then 1 in kCheckedEvery
constexpr uint64_t kCheckedEvery = 128;

std::atomic<bool> g_off{false};
std::atomic<uint64_t> g_calls{0};
std::atomic<uint64_t> g_mismatches{0};
struct Timing {
  std::atomic<uint64_t> calls{0}, vertices{0}, ns{0};
};
Timing g_native[4], g_game[4];  // per kernel; the game's timed in the checked calls
std::atomic<int64_t> g_next_report{0};

// Vectors are kept with lane i = PowerPC element i (bytes in memory order, then each word byte-swapped).
inline F4 ToF4(uint8x16_t bytes) { return vreinterpretq_f32_u8(vrev32q_u8(bytes)); }
inline uint8x16_t ToBytes(F4 v) { return vrev32q_u8(vreinterpretq_u8_f32(v)); }
inline F4 LoadF4(const uint8_t* p) { return ToF4(vld1q_u8(p)); }
// vperm: byte i of the result is byte (c[i] & 31) of a:b, in memory order.
inline uint8x16_t Perm(uint8x16_t a, uint8x16_t b, uint8x16_t c) {
  const uint8x16x2_t table{{a, b}};
  return vqtbl2q_u8(table, vandq_u8(c, vdupq_n_u8(31)));
}
// lvlx: the bytes from ea to the end of its 16-byte block, left-aligned, the rest zero.
inline uint8x16_t LoadLeft(const uint8_t* base, uint32_t ea) {
  uint8_t bytes[16] = {};
  std::memcpy(bytes, base + ea, 16 - (ea & 15));
  return vld1q_u8(bytes);
}

// vupkd3d128 type 8 of one word, as the recompiled code does it.
inline F4 Unpack(uint32_t w) {
  const int32_t x = int32_t(w << 22) >> 22, y = int32_t(w << 12) >> 22, z = int32_t(w << 2) >> 22;
  const uint32_t lanes[4] = {x == -512 ? 0x7FC00000u : uint32_t(x + 0x40400000), uint32_t(y + 0x40400000),
                             uint32_t(z + 0x40400000), (w >> 30) | 0x3F800000u};
  return vreinterpretq_f32_u32(vld1q_u32(lanes));
}
// vpkd3d128 type 2 into one word, as the recompiled code does it.
inline uint32_t Clamp(float v, float lo, float hi) {
  const float c = v < lo ? lo : (v > hi ? hi : v);
  uint32_t u;
  std::memcpy(&u, &c, 4);
  return u;
}
inline uint32_t Pack(F4 v) {
  return (Clamp(vgetq_lane_f32(v, 0), kPack2101010_Min10, kPack2101010_Max10) & 0x3FF) |
         (Clamp(vgetq_lane_f32(v, 1), kPack2101010_Min10, kPack2101010_Max10) & 0x3FF) << 10 |
         (Clamp(vgetq_lane_f32(v, 2), kPack2101010_Min10, kPack2101010_Max10) & 0x3FF) << 20 |
         (Clamp(vgetq_lane_f32(v, 3), kPack2101010_Min2, kPack2101010_Max2) & 0x3) << 30;
}

struct Constants {
  F4 k8, k9, k10, k11, one;
  uint8x16_t weight_fill, weight_control, position_control;
};

struct Bone {
  F4 row[4];
};
// The rows as lvx reads them: each address rounded down to 16.
inline Bone Rows(const uint8_t* base, uint32_t address) {
  Bone b;
  for (uint32_t k = 0; k < 4; ++k) {
    b.row[k] = LoadF4(base + ((address + 16 * k) & ~15u));
  }
  return b;
}
inline F4 Transform(const Bone& b, F4 p) {
  F4 t = vfmaq_laneq_f32(b.row[3], b.row[2], p, 2);
  t = vfmaq_laneq_f32(t, b.row[1], p, 1);
  return vfmaq_laneq_f32(t, b.row[0], p, 0);
}
inline F4 Rotate(const Bone& b, F4 n) {
  F4 r = vmulq_laneq_f32(b.row[2], n, 2);
  r = vfmaq_laneq_f32(r, b.row[1], n, 1);
  return vfmaq_laneq_f32(r, b.row[0], n, 0);
}

template <int kBones>
void Run(const PPCContext& ctx, uint8_t* base, const Constants& k, uint8_t* out) {
  constexpr uint32_t kRecord = 2 + 4 * (kBones - 1);
  const uint32_t count = ctx.r5.u32;
  for (uint32_t i = 0; i < count; ++i) {
    const uint8_t* vertex = base + ctx.r3.u32 + 32 * i;
    const uint32_t record = ctx.r4.u32 + kRecord * i;
    const F4 p = LoadF4(vertex);
    const uint8x16_t raw = vld1q_u8(vertex + 16);
    const Bone a = Rows(base, ctx.r6.u32 + rex::memory::load_and_swap<uint16_t>(base + record));
    F4 position = Transform(a, p);
    if constexpr (kBones > 1) {
      F4 sum{}, acc{};
      for (int j = 1; j < kBones; ++j) {
        const uint32_t offset = rex::memory::load_and_swap<uint16_t>(base + record + 2 + 4 * (j - 1));
        const uint8x16_t lanes = Perm(LoadLeft(base, record + 4 * j), k.weight_fill, k.weight_control);
        const F4 w = vcvtq_n_f32_u32(vreinterpretq_u32_u8(vrev32q_u8(lanes)), 16);
        const F4 b = Transform(Rows(base, ctx.r6.u32 + offset), p);
        acc = j == 1 ? vmulq_f32(b, w) : vfmaq_f32(acc, w, b);
        sum = j == 1 ? w : vaddq_f32(sum, w);
      }
      position = vfmaq_f32(acc, vsubq_f32(k.one, sum), position);
    }
    const uint8x16_t w_splat = ToBytes(vdupq_laneq_f32(p, 3));
    vst1q_u8(out + 32 * i, Perm(ToBytes(position), w_splat, k.position_control));
    const F4 n2 = vfmaq_f32(k.k10, Unpack(rex::memory::load_and_swap<uint32_t>(vertex + 24)), k.k11);
    const F4 n3 = vfmaq_f32(k.k10, Unpack(rex::memory::load_and_swap<uint32_t>(vertex + 28)), k.k11);
    const uint32_t packed2 = __builtin_bswap32(Pack(vfmaq_f32(k.k9, Rotate(a, n2), k.k8)));
    const uint32_t packed3 = __builtin_bswap32(Pack(vfmaq_f32(k.k9, Rotate(a, n3), k.k8)));
    uint8_t tail[16];
    vst1q_u8(tail, raw);
    std::memcpy(tail + 8, &packed2, 4);
    std::memcpy(tail + 12, &packed3, 4);
    std::memcpy(out + 32 * i + 16, tail, 16);
  }
}

bool Close(float a, float b) {
  if (std::memcmp(&a, &b, 4) == 0) return true;
  return std::fabs(a - b) <= 1e-4f + 1e-5f * std::fabs(b);
}
bool ClosePacked(uint32_t a, uint32_t b) {
  if ((a >> 30) != (b >> 30)) return false;
  for (int s = 0; s < 30; s += 10) {
    const int d = int(((a >> s) - (b >> s)) & 0x3FF);
    if (d > 2 && d < 1022) return false;
  }
  return true;
}
// Index of the first vertex beyond the tolerance, or -1.
int64_t Compare(const uint8_t* mine, const uint8_t* game, uint32_t count) {
  for (uint32_t i = 0; i < count; ++i) {
    const uint8_t* m = mine + 32 * i;
    const uint8_t* g = game + 32 * i;
    for (int l = 0; l < 4; ++l) {
      if (!Close(rex::memory::load_and_swap<float>(m + 4 * l), rex::memory::load_and_swap<float>(g + 4 * l))) return i;
    }
    if (std::memcmp(m + 16, g + 16, 8) != 0) return i;
    for (int wd = 24; wd < 32; wd += 4) {
      if (!ClosePacked(rex::memory::load_and_swap<uint32_t>(m + wd), rex::memory::load_and_swap<uint32_t>(g + wd))) {
        return i;
      }
    }
  }
  return -1;
}

void Report() {
  const int64_t now = std::chrono::steady_clock::now().time_since_epoch().count();
  int64_t next = g_next_report.load(std::memory_order_relaxed);
  if (now < next || !g_next_report.compare_exchange_strong(next, now + int64_t(10'000'000'000))) {
    return;
  }
  if (next == 0) return;  // first call: arm only
  static uint64_t said[4][2][3] = {};
  std::string line;
  for (int kernel = 0; kernel < 4; ++kernel) {
    for (int side = 0; side < 2; ++side) {
      Timing& t = side ? g_game[kernel] : g_native[kernel];
      const uint64_t v[3] = {t.calls.load(std::memory_order_relaxed), t.vertices.load(std::memory_order_relaxed),
                             t.ns.load(std::memory_order_relaxed)};
      const uint64_t calls = v[0] - said[kernel][side][0], vertices = v[1] - said[kernel][side][1],
                     ns = v[2] - said[kernel][side][2];
      std::memcpy(said[kernel][side], v, sizeof(v));
      line += fmt::format("{}{} {}: {} calls, {:.1f} ns a vertex", side ? " | " : (kernel ? "; " : ""),
                          kernel + 1, side ? "game (checked calls)" : "native", calls,
                          vertices ? double(ns) / double(vertices) : 0.0);
    }
  }
  REXLOG_INFO("[native_skin] last 10 s (bones: calls, cost){}: {}", g_off.load() ? " OFF after a mismatch" : "", line);
}

template <int kBones>
void Kernel(PPCContext& ctx, uint8_t* base, void (*game)(PPCContext&, uint8_t*)) {
  static const bool wanted = REXCVAR_GET(mw2mp_native_skin);
  const uint32_t count = ctx.r5.u32;
  // lvx/stvx take the addresses rounded down to 16: anything not 16-aligned stays with the game's code.
  if (!wanted || g_off.load(std::memory_order_relaxed) || count == 0 || ((ctx.r3.u32 | ctx.r7.u32) & 15) != 0) {
    game(ctx, base);
    return;
  }
  Report();
  Constants k;
  k.k8 = LoadF4(base + kScale8);
  k.k9 = LoadF4(base + kBias9);
  k.k10 = LoadF4(base + kBias10);
  k.k11 = LoadF4(base + kScale11);
  k.one = LoadF4(base + kOne);
  k.weight_fill = vld1q_u8(base + kWeightFill);
  k.weight_control = vld1q_u8(base + kWeightControl);
  k.position_control = vld1q_u8(base + kPositionControl);
  const uint64_t call = g_calls.fetch_add(1, std::memory_order_relaxed);
  const uint64_t in_lo = ctx.r3.u32, in_hi = in_lo + 32ull * count, out_lo = ctx.r7.u32, out_hi = out_lo + 32ull * count;
  const bool overlap = out_lo < in_hi && in_lo < out_hi;
  const bool check = !overlap && (call < kCheckedFirst || call % kCheckedEvery == 0);
  uint8_t* const out = base + ctx.r7.u32;
  thread_local std::vector<uint8_t> mine;
  if (check) {
    mine.resize(size_t(count) * 32);
  }
  const auto t0 = std::chrono::steady_clock::now();
  Run<kBones>(ctx, base, k, check ? mine.data() : out);
  const auto t1 = std::chrono::steady_clock::now();
  Timing& n = g_native[kBones - 1];
  n.calls.fetch_add(1, std::memory_order_relaxed);
  n.vertices.fetch_add(count, std::memory_order_relaxed);
  n.ns.fetch_add(uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count()),
                 std::memory_order_relaxed);
  if (!check) {
    return;
  }
  const uint32_t vertices = ctx.r3.u32, records = ctx.r4.u32, bones = ctx.r6.u32;
  game(ctx, base);
  const auto t2 = std::chrono::steady_clock::now();
  Timing& g = g_game[kBones - 1];
  g.calls.fetch_add(1, std::memory_order_relaxed);
  g.vertices.fetch_add(count, std::memory_order_relaxed);
  g.ns.fetch_add(uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(t2 - t1).count()),
                 std::memory_order_relaxed);
  const int64_t bad = Compare(mine.data(), out, count);
  if (bad >= 0 && !g_off.exchange(true)) {
    g_mismatches.fetch_add(1, std::memory_order_relaxed);
    const uint8_t* m = mine.data() + 32 * bad;
    const uint8_t* o = out + 32 * bad;
    std::string a, b;
    for (int i = 0; i < 32; i += 4) {
      a += fmt::format(" {:08X}", rex::memory::load_and_swap<uint32_t>(m + i));
      b += fmt::format(" {:08X}", rex::memory::load_and_swap<uint32_t>(o + i));
    }
    REXLOG_ERROR("[native_skin] MISMATCH, {} bones, call {}, vertex {} of {} (vertices {:08X} records {:08X} bones "
                 "{:08X}): native{} | game{}. The rewrite is off for this session",
                 kBones, call, bad, count, vertices, records, bones, a, b);
  }
}

}  // namespace

REX_HOOK_RAW(sub_823D7178) { Kernel<1>(ctx, base, __imp__sub_823D7178); }
REX_HOOK_RAW(sub_823D7318) { Kernel<2>(ctx, base, __imp__sub_823D7318); }
REX_HOOK_RAW(sub_823D7530) { Kernel<3>(ctx, base, __imp__sub_823D7530); }
REX_HOOK_RAW(sub_823D7770) { Kernel<4>(ctx, base, __imp__sub_823D7770); }

#endif
