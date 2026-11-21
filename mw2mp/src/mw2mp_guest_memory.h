// How the generated code reaches guest memory in this port. Appended to the generated code's precompiled header
// (mw2mp/CMakeLists.txt, mw2sp/CMakeLists.txt), after mw2mp_pch.h / mw2sp_pch.h.
//
// The code generator makes every guest load and store volatile (sdk/resources/templates/codegen/pch_h.inja,
// REX_LOAD_* / REX_STORE_*). That keeps them in program order and stops GCC from removing any of them: the game's
// code reloads the same float constants and fields over and over (compiled for a CPU where a load was cheap), and
// each reload stays a load, a byte swap and often a conversion. Compiled without volatile, a typical generated file
// came out 8 % smaller, with 156 of its 207 functions shorter, some by three quarters.
//
// With MW2MP_GUEST_MEMORY_PLAIN (CMake options MW2MP_GUEST_MEMORY_PLAIN / MW2SP_GUEST_MEMORY_PLAIN: off in CMake, on in
// both build_switch.sh) the guest loads and stores are ordinary memory accesses, through may_alias types (the generated
// code is built with strict aliasing, and the guest reads the same bytes as different widths). What volatile also gave,
// and the guest relies on, is kept by REX_GUEST_BARRIER(), a compiler barrier on guest memory that
// tools/guest_barriers.py puts after every label (every branch target, so no load is kept in a register across a
// loop's back edge: a spin on a flag still reads it each time) and after every sync, lwsync, eieio, isync and db16cyc
// (the guest's ordering points, which the code generator emits as nothing). Between two barriers GCC may merge, reorder
// or drop loads and stores of one straight-line block, which another thread could not tell from a different
// interleaving. MMIO accesses (REX_MM_*), the reservation loads of lwarx and the stwcx compare-and-swap are not changed.
// Without the option REX_GUEST_BARRIER() is empty and the code is what the generator wrote.

#pragma once

#if defined(MW2MP_GUEST_MEMORY_PLAIN)

// The barrier says the asm may read and write all of guest memory (an object as large as the guest's 4 GB, plus the
// 0xE0000000 view's page offset), not "memory": ctx is a __restrict parameter, so the PPCContext fields the compiler
// keeps in host registers stay there instead of going out to memory and back at every label.
typedef struct {
  unsigned char bytes[0x100001000ull];
} mw2_guest_all;
#define REX_GUEST_BARRIER() __asm__ __volatile__("" : "+m"(*(mw2_guest_all*)base))

typedef u8 __attribute__((may_alias)) mw2_guest_u8;
typedef u16 __attribute__((may_alias)) mw2_guest_u16;
typedef u32 __attribute__((may_alias)) mw2_guest_u32;
typedef u64 __attribute__((may_alias)) mw2_guest_u64;

#undef REX_LOAD_U8
#undef REX_LOAD_U16
#undef REX_LOAD_U32
#undef REX_LOAD_U64
#undef REX_STORE_U8
#undef REX_STORE_U16
#undef REX_STORE_U32
#undef REX_STORE_U64
#define REX_LOAD_U8(x) (*(mw2_guest_u8*)(base + (u32)(x) + REX_PHYS_HOST_OFFSET(x)))
#define REX_LOAD_U16(x) __builtin_bswap16(*(mw2_guest_u16*)(base + (u32)(x) + REX_PHYS_HOST_OFFSET(x)))
#define REX_LOAD_U32(x) __builtin_bswap32(*(mw2_guest_u32*)(base + (u32)(x) + REX_PHYS_HOST_OFFSET(x)))
#define REX_LOAD_U64(x) __builtin_bswap64(*(mw2_guest_u64*)(base + (u32)(x) + REX_PHYS_HOST_OFFSET(x)))
#define REX_STORE_U8(x, y) (*(mw2_guest_u8*)(base + (u32)(x) + REX_PHYS_HOST_OFFSET(x)) = (y))
#define REX_STORE_U16(x, y) (*(mw2_guest_u16*)(base + (u32)(x) + REX_PHYS_HOST_OFFSET(x)) = __builtin_bswap16(y))
#define REX_STORE_U32(x, y) (*(mw2_guest_u32*)(base + (u32)(x) + REX_PHYS_HOST_OFFSET(x)) = __builtin_bswap32(y))
#define REX_STORE_U64(x, y) (*(mw2_guest_u64*)(base + (u32)(x) + REX_PHYS_HOST_OFFSET(x)) = __builtin_bswap64(y))

#else

#define REX_GUEST_BARRIER() ((void)0)

#endif

// Diagnostic (CMake MW2_WATCH_STORES, mw2sp/build_switch.sh MW2_WATCH_STORES=1; never in a release): every guest
// store, and every vector access through REX_RAW_ADDR, checks whether it lands in the physical range the renderer
// published (the model lighting volume, mw2mp_native_draws.cpp UpdateVolume); a hit records the host return address
// and the guest lr, reported in the C3 3D textures line. Finds the volume's writer, so its pages can be marked instead
// of hashing all 2 MB on every update.
#if defined(MW2_WATCH_STORES)
extern "C" unsigned mw2_watch_lo;
extern "C" unsigned mw2_watch_size;
extern "C" void mw2_watch_hit(unsigned guest, unsigned long long guest_lr);
static inline u32 mw2_watch_check(u32 x, unsigned long long guest_lr) {
  if (x >= 0xA0000000u &&
      (x & 0x1FFFFFFFu) + (x >= 0xE0000000u ? 0x1000u : 0u) - mw2_watch_lo < mw2_watch_size) [[unlikely]] {
    mw2_watch_hit(x, guest_lr);
  }
  return x;
}
#undef REX_RAW_ADDR
#undef REX_STORE_U8
#undef REX_STORE_U16
#undef REX_STORE_U32
#undef REX_STORE_U64
#define REX_RAW_ADDR(x) (base + mw2_watch_check((u32)(x), ctx.lr) + REX_PHYS_HOST_OFFSET(x))
#define REX_STORE_U8(x, y) (*(volatile u8*)REX_RAW_ADDR(x) = (y))
#define REX_STORE_U16(x, y) (*(volatile u16*)REX_RAW_ADDR(x) = __builtin_bswap16(y))
#define REX_STORE_U32(x, y) (*(volatile u32*)REX_RAW_ADDR(x) = __builtin_bswap32(y))
#define REX_STORE_U64(x, y) (*(volatile u64*)REX_RAW_ADDR(x) = __builtin_bswap64(y))
#endif
