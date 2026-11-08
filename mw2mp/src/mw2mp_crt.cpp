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
