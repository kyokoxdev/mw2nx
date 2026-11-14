// MW2's memory copy and fill routines as native code ([rexcrt] in mw2mp/overrides.toml), as bo2-360-nx's
// app/src/bo2zm_crt.cpp.
//
// The codegen drops the recompiled bodies of mw2mp_memcpy (0x8241A9F0) and mw2mp_memset (0x8241A890) and calls these
// from every direct call and the dispatch table. r3 is left as it came in, which is dst: what both routines return.
// No other register is written (the 360 routines only clobber volatile ones). Copies use memmove semantics, a
// superset of the CRT's forward-only memcpy. Each write is stamped for the native renderer (mw2mp_page_writes.h).

#include <cstdint>
#include <cstring>

#include <rex/hook.h>
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
