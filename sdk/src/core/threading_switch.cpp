/**
 * @file        rex/core/threading_switch.cpp
 * @brief       Thread shim for Horizon: affinity, priority and suspension
 *
 * See threading_switch.h for why this layer exists and for the priority
 * scheme, which is what decides whether the game stutters or not.
 */

#include <rex/platform.h>

#if REX_PLATFORM_SWITCH

#include "threading_switch.h"

/*
 * switch.h is included here, privately. This file uses none of the rex::thread
 * types, so libnx polluting the global namespace (Thread, Timer, Event,
 * Handle...) does no harm. The header above does not include it, precisely so
 * that threading_posix.cpp does not suffer from it.
 */
#include <switch.h>

#include <errno.h>
#include <malloc.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <algorithm>
#include <new>
#include <utility>
#include <vector>

#include <mutex>
#include <unordered_map>

namespace {

/*
 * Registry pthread_t -> Horizon Handle.
 *
 * libnx exposes no way to get the handle of another thread, and the SDK
 * changes the priority and affinity of other threads, so it has to be tracked
 * by hand.
 *
 * Not a hot path: it is touched when threads are created and destroyed, never
 * per frame. A plain mutex is more than enough.
 */
struct Record {
  std::mutex m;
  std::unordered_map<uintptr_t, RexSwitchHandle> map_entry;
};

Record& record() {
  static Record r;
  return r;
}

}  // namespace

extern "C" {

void RexSwitchRegisterCurrentThread(void) {
  auto& r = record();
  std::lock_guard<std::mutex> lock(r.m);
  r.map_entry[reinterpret_cast<uintptr_t>(pthread_self())] = threadGetCurHandle();
}

void RexSwitchUnregisterCurrentThread(void) {
  auto& r = record();
  std::lock_guard<std::mutex> lock(r.m);
  r.map_entry.erase(reinterpret_cast<uintptr_t>(pthread_self()));
}

RexSwitchHandle RexSwitchHandleFor(pthread_t thread) {
  if (pthread_equal(thread, pthread_self()))
    return threadGetCurHandle();

  auto& r = record();
  std::lock_guard<std::mutex> lock(r.m);
  auto it = r.map_entry.find(reinterpret_cast<uintptr_t>(thread));
  return it == r.map_entry.end() ? REX_SWITCH_INVALID_HANDLE : it->second;
}

/* --- affinity ---------------------------------------------------------- */

int pthread_getaffinity_np(pthread_t thread, size_t size, cpu_set_t* set) {
  if (set == nullptr || size < sizeof(cpu_set_t))
    return EINVAL;

  RexSwitchHandle h = RexSwitchHandleFor(thread);
  if (h == REX_SWITCH_INVALID_HANDLE)
    return ESRCH;

  s32 preferred = 0;
  u64 mask = 0;
  if (R_FAILED(svcGetThreadCoreMask(&preferred, &mask, h)))
    return EINVAL;

  set->bits = mask;
  return 0;
}

int pthread_setaffinity_np(pthread_t thread, size_t size, const cpu_set_t* set) {
  if (set == nullptr || size < sizeof(cpu_set_t))
    return EINVAL;

  RexSwitchHandle h = RexSwitchHandleFor(thread);
  if (h == REX_SWITCH_INVALID_HANDLE)
    return ESRCH;

  /*
   * Horizon also wants a preferred core. The lowest one in the mask is
   * taken; -1 would mean "whatever it already had", which is not what the
   * POSIX API being imitated asks for.
   */
  s32 preferred = -1;
  for (int i = 0; i < 4; i++) {
    if (set->bits & (1ull << i)) { preferred = i; break; }
  }
  if (preferred < 0)
    return EINVAL;  /* empty mask: POSIX does not allow it either */

  /*
   * Only 4 cores exist. Asking for more is not a caller error (the SDK
   * passes 64-bit masks), so it is trimmed silently.
   */
  const u32 mask = static_cast<u32>(set->bits & 0xF);

  if (R_FAILED(svcSetThreadCoreMask(h, preferred, mask)))
    return EPERM;
  return 0;
}

/*
 * --- priority ------------------------------------------------------------
 *
 * sched_param::sched_priority is passed straight through as the Horizon
 * priority, without inverting the scale. This is deliberate: callers using the
 * REX_SWITCH_PRIO_* constants expect Horizon numbers, where lower means higher
 * priority. Inverting it here to look like POSIX would only turn the priority
 * scheme upside down, which is exactly the bug that causes stutter and audio
 * dropouts.
 */

int pthread_getschedparam(pthread_t thread, int* policy, struct sched_param* param) {
  RexSwitchHandle h = RexSwitchHandleFor(thread);
  if (h == REX_SWITCH_INVALID_HANDLE)
    return ESRCH;

  s32 prio = 0;
  if (R_FAILED(svcGetThreadPriority(&prio, h)))
    return EINVAL;

  if (policy) *policy = SCHED_FIFO;
  if (param) param->sched_priority = prio;
  return 0;
}

int pthread_setschedparam(pthread_t thread, int policy, const struct sched_param* param) {
  (void)policy;
  if (param == nullptr)
    return EINVAL;

  RexSwitchHandle h = RexSwitchHandleFor(thread);
  if (h == REX_SWITCH_INVALID_HANDLE)
    return ESRCH;

  if (R_FAILED(svcSetThreadPriority(h, static_cast<u32>(param->sched_priority))))
    return EPERM;
  return 0;
}

int pthread_attr_setschedpolicy(pthread_attr_t* attr, int policy) {
  (void)attr;
  (void)policy;
  /* Horizon has a single policy. Accepting and doing nothing is correct. */
  return 0;
}

/* --- robust mutexes: not available -------------------------------------- */

int pthread_mutexattr_setrobust(pthread_mutexattr_t* attr, int robustness) {
  (void)attr;
  /*
   * PTHREAD_MUTEX_STALLED is accepted (the real behavior), and ROBUST is
   * accepted without providing it: the SDK uses it as a safeguard against a
   * thread dying while holding the mutex, which should not happen in the guest.
   */
  if (robustness != PTHREAD_MUTEX_STALLED && robustness != PTHREAD_MUTEX_ROBUST)
    return EINVAL;
  return 0;
}

int pthread_mutex_consistent(pthread_mutex_t* mutex) {
  (void)mutex;
  return 0;
}

int pthread_mutexattr_getrobust(const pthread_mutexattr_t* attr, int* robustness) {
  (void)attr;
  if (robustness) *robustness = PTHREAD_MUTEX_STALLED;
  return 0;
}

/*
 * --- suspension ----------------------------------------------------------
 *
 * The SDK suspends threads by sending them a signal. Horizon does not deliver
 * signals, so that cannot work however much shimming is added: the system
 * mechanism has to be used.
 *
 * A difference in semantics worth knowing: svcSetThreadActivity stops the
 * thread wherever it is, not at a safe point chosen by the thread itself. For
 * what the SDK does with this (suspending guest threads, which is what the
 * original console did) that is correct. For any use that needs the thread to
 * stop at a specific place, it is not.
 */

int RexSwitchPauseThread(pthread_t thread) {
  RexSwitchHandle h = RexSwitchHandleFor(thread);
  if (h == REX_SWITCH_INVALID_HANDLE)
    return ESRCH;
  if (pthread_equal(thread, pthread_self()))
    return EDEADLK;  /* pausing oneself cannot be undone */
  return R_SUCCEEDED(svcSetThreadActivity(h, ThreadActivity_Paused)) ? 0 : EPERM;
}

int RexSwitchResumeThread(pthread_t thread) {
  RexSwitchHandle h = RexSwitchHandleFor(thread);
  if (h == REX_SWITCH_INVALID_HANDLE)
    return ESRCH;
  return R_SUCCEEDED(svcSetThreadActivity(h, ThreadActivity_Runnable)) ? 0 : EPERM;
}

/* --- priority of the current thread ------------------------------------ */

uint32_t RexSwitchCurrentThreadId(void) {
  /* The current thread's handle serves as a unique id, stable while it lives. */
  return static_cast<uint32_t>(threadGetCurHandle());
}

/*
 * threadGetSelf() only returns something for threads that libnx created with threadCreate. Ours
 * come from pthread_create (rex::thread::Thread::Create -> PosixThread), so if the TLS does not carry
 * libnx's ThreadVars, this used to be a silent no-op: the priority was not applied and the log said
 * it was, because it only printed the cvar value.
 *
 * CUR_THREAD_HANDLE (0xFFFF8000) is the pseudo-handle that the Horizon kernel always understands as
 * "the calling thread", wherever it came from. The result is returned so that it can be checked in
 * the log instead of assumed.
 */
bool RexSwitchSetCurrentThreadPriorityOk(int priority) {
  Thread* yo = threadGetSelf();
  Handle h = (yo != nullptr) ? yo->handle : CUR_THREAD_HANDLE;
  if (R_SUCCEEDED(svcSetThreadPriority(h, static_cast<u32>(priority)))) {
    return true;
  }
  if (h != CUR_THREAD_HANDLE) {
    return R_SUCCEEDED(svcSetThreadPriority(CUR_THREAD_HANDLE, static_cast<u32>(priority)));
  }
  return false;
}

void RexSwitchSetCurrentThreadPriority(int priority) {
  (void)RexSwitchSetCurrentThreadPriorityOk(priority);
}

/*
 * Preferred core of the current thread.
 *
 * All our threads run with "preferred core -1", which on Horizon means the process default core:
 * the same one for all of them. With the ring at 94.7 % of a core and the game's main thread at
 * 88.2 %, both prefer the same place and the kernel has to keep moving them. Each migration throws
 * away the thread's L1 and part of its L2, and that does not show in the mean: it shows in the
 * variance, which is exactly what the player notices.
 *
 * The mask is not touched. The thread's existing mask is passed, so this only says "start here",
 * not "you may only run here": if the core is busy, the kernel can still move the thread. That is
 * why it cannot cause starvation, which is what makes real affinity pinning dangerous.
 *
 * Returns false if the kernel does not accept it (for example if the core is not in the process
 * mask: that is the "Too few processor cores" error seen when trying to use the fourth core).
 */
bool RexSwitchSetCurrentThreadCore(int core) {
  Thread* yo = threadGetSelf();
  Handle h = (yo != nullptr) ? yo->handle : CUR_THREAD_HANDLE;
  s32 preferred = 0;
  u64 mask = 0;
  if (R_FAILED(svcGetThreadCoreMask(&preferred, &mask, h))) {
    if (h == CUR_THREAD_HANDLE ||
        R_FAILED(svcGetThreadCoreMask(&preferred, &mask, CUR_THREAD_HANDLE))) {
      return false;
    }
    h = CUR_THREAD_HANDLE;
  }
  if (core >= 0 && (mask & (u64(1) << core)) == 0) {
    return false;  // that core is not this process's: do not force it
  }
  return R_SUCCEEDED(svcSetThreadCoreMask(h, core, mask));
}

/*
 * The real pin, with the mask reduced to a single core.
 *
 * Measured: a preferred core is not enough. Horizon accepts it and moves the thread anyway (0.24
 * migrations per loop, wherever it is set). This takes the other cores away from the thread.
 *
 * Different from RexSwitchSetCurrentThreadCore and more dangerous: if that core saturates, the
 * thread does not get to run. That is why it sits behind a cvar that is off by default and is
 * checked in the log. See docs/platform-notes.md (Threads).
 */
bool RexSwitchPinCurrentThreadToCore(int core) {
  if (core < 0) {
    return false;
  }
  Thread* yo = threadGetSelf();
  Handle h = (yo != nullptr) ? yo->handle : CUR_THREAD_HANDLE;
  s32 preferred = 0;
  u64 mask = 0;
  if (R_FAILED(svcGetThreadCoreMask(&preferred, &mask, h))) {
    if (h == CUR_THREAD_HANDLE ||
        R_FAILED(svcGetThreadCoreMask(&preferred, &mask, CUR_THREAD_HANDLE))) {
      return false;
    }
    h = CUR_THREAD_HANDLE;
  }
  const u64 only_that = u64(1) << core;
  if ((mask & only_that) == 0) {
    return false;  // that core is not this process's
  }
  return R_SUCCEEDED(svcSetThreadCoreMask(h, core, only_that));
}

/*
 * Which core the calling thread is running on right now. This is what allows counting migrations:
 * it is read on every loop iteration and counted each time it changes. There is no syscall to ask
 * it about another thread, so each thread has to look at itself.
 */
int RexSwitchCurrentCore(void) { return int(svcGetCurrentProcessorNumber()); }

bool RexSwitchSetCurrentThreadCoreMask(uint64_t mask) {
  Thread* yo = threadGetSelf();
  Handle h = (yo != nullptr) ? yo->handle : CUR_THREAD_HANDLE;
  s32 preferred = 0;
  u64 current = 0;
  if (R_FAILED(svcGetThreadCoreMask(&preferred, &current, h))) {
    if (h == CUR_THREAD_HANDLE || R_FAILED(svcGetThreadCoreMask(&preferred, &current, CUR_THREAD_HANDLE))) {
      return false;
    }
    h = CUR_THREAD_HANDLE;
  }
  // Only the process's cores (the kernel refuses any other; see RexSwitchSetCurrentThreadCore).
  u64 allowed = 0;
  if (R_FAILED(svcGetInfo(&allowed, InfoType_CoreMask, CUR_PROCESS_HANDLE, 0))) {
    allowed = current;
  }
  const u64 new_mask = mask & allowed & 0xF;
  if (new_mask == 0) {
    return false;
  }
  if (preferred < 0 || preferred > 3 || (new_mask & (u64(1) << preferred)) == 0) {
    preferred = __builtin_ctzll(new_mask);
  }
  return R_SUCCEEDED(svcSetThreadCoreMask(h, preferred, u32(new_mask)));
}

void RexSwitchHeapStats(uint64_t* used, uint64_t* available) {
  extern char* fake_heap_end;  // libnx: the end of the heap newlib's sbrk hands out
  const struct mallinfo info = mallinfo();
  const uintptr_t brk = uintptr_t(sbrk(0));
  const uintptr_t end = uintptr_t(fake_heap_end);
  *used = uint64_t(info.uordblks);
  *available = uint64_t(info.fordblks) + (end > brk ? end - brk : 0);
}

namespace {
struct Pieced {
  uint8_t* view = nullptr;
  bool reserved = false;                         // view is the boot reservation (g_reserve)
  std::vector<std::pair<void*, size_t>> pieces;  // heap block, bytes; mapped in order from view
  size_t mapped = 0;                             // pieces mapped so far
};

// A stretch of the stack region reserved at boot for the pieced view, so a large one never depends on the region
// still having that much free later (threads map their stacks there). Address space only: no memory behind it. Static
// constructors run after libnx's virtmemSetup (__libnx_init: virtmemSetup, ..., __libc_init_array). One view at a
// time uses it; another one, or one larger than it, falls back to virtmemFindStack.
constexpr size_t kReserveWanted = size_t(256) << 20;
uint8_t* g_reserve = nullptr;
size_t g_reserve_bytes = 0;
bool g_reserve_busy = false;  // under virtmemLock

const bool g_reserve_made = [] {
  u64 region = 0;
  if (R_FAILED(svcGetInfo(&region, InfoType_StackRegionSize, CUR_PROCESS_HANDLE, 0))) {
    return false;
  }
  const size_t bytes = std::min(kReserveWanted, size_t(region / 4)) & ~size_t(0xFFF);  // leave most to the threads
  if (!bytes) {
    return false;
  }
  virtmemLock();
  if (void* at = virtmemFindStack(bytes, 0x4000); at && virtmemAddReservation(at, bytes)) {
    g_reserve = static_cast<uint8_t*>(at);
    g_reserve_bytes = bytes;
  }
  virtmemUnlock();
  return g_reserve != nullptr;
}();

void UnmapPieced(Pieced* p) {
  size_t at = 0;
  for (size_t i = 0; i < p->mapped; ++i) {
    svcUnmapMemory(p->view + at, p->pieces[i].first, p->pieces[i].second);
    at += p->pieces[i].second;
  }
  for (const auto& [block, n] : p->pieces) {
    free(block);  // only once unmapped: the heap's pages are not accessible while mapped elsewhere
  }
  if (p->reserved) {
    virtmemLock();
    g_reserve_busy = false;
    virtmemUnlock();
  }
  delete p;
}

// Run mw2sp_033: 108 MB failed with 674 MB of heap left, and nothing said whether the heap pieces, the free range in
// the stack region or one svcMapMemory was the step that failed.
char g_pieced_note[192] = "";

size_t SmallestPiece(const Pieced* p) {
  size_t smallest = 0;
  for (const auto& piece : p->pieces) {
    smallest = smallest ? std::min(smallest, piece.second) : piece.second;
  }
  return smallest;
}
}  // namespace

const char* RexSwitchPiecedNote(void) { return g_pieced_note; }

void* RexSwitchAllocPieced(size_t bytes, void** handle) {
  *handle = nullptr;
  g_pieced_note[0] = '\0';
  const size_t total = (bytes + 0xFFF) & ~size_t(0xFFF);
  Pieced* p = new (std::nothrow) Pieced;
  if (!p || !total) {
    delete p;
    return nullptr;
  }
  size_t got = 0;
  // 1 MB, not 8: the heap's best fit hands out the smallest chunk that fits, so small pieces use up the fragments and
  // leave the large blocks to the game. Run mw2sp_035 (and 029, 033, 034): 8 MB pieces took them at a level change,
  // and the level load (32 MB texture pool blocks, GPU buffers) ran out of memory within a second.
  size_t piece = size_t(1) << 20;
  try {
    while (got < total) {
      const size_t want = std::min(piece, total - got);  // both multiples of 4 KB
      if (void* block = aligned_alloc(0x1000, want)) {
        p->pieces.emplace_back(block, want);
        got += want;
      } else if (piece > (size_t(64) << 10)) {
        piece >>= 1;
      } else {
        break;
      }
    }
  } catch (const std::bad_alloc&) {
  }
  if (got < total) {
    snprintf(g_pieced_note, sizeof(g_pieced_note),
             "heap pieces: %zu KB of %zu KB in %zu pieces (smallest %zu KB), none of 64 KB left", got >> 10,
             total >> 10, p->pieces.size(), SmallestPiece(p) >> 10);
    UnmapPieced(p);
    return nullptr;
  }
  virtmemLock();
  const bool reserve_fits = g_reserve && !g_reserve_busy && total <= g_reserve_bytes;
  if (reserve_fits) {
    p->view = g_reserve;
    p->reserved = true;
    g_reserve_busy = true;
  } else {
    p->view = static_cast<uint8_t*>(virtmemFindStack(total, 0x4000));
  }
  bool ok = p->view != nullptr;
  if (!ok) {
    snprintf(g_pieced_note, sizeof(g_pieced_note), "no free %zu KB range in the stack region (%zu pieces)",
             total >> 10, p->pieces.size());
  }
  // A heap piece the kernel will not map (a page state svcMapMemory refuses, such as an attribute a freed GPU buffer
  // may leave behind) is held aside, so the heap cannot hand it back, and a new piece of the same size takes its place.
  constexpr int kMaxRefused = 32;
  std::pair<void*, size_t> refused[kMaxRefused];
  int refused_count = 0;
  Result first_refusal = 0;
  for (size_t at = 0; ok && p->mapped < p->pieces.size();) {
    auto& [block, n] = p->pieces[p->mapped];
    const Result rc = svcMapMemory(p->view + at, block, n);
    if (R_SUCCEEDED(rc)) {
      at += n;
      ++p->mapped;
      continue;
    }
    void* other = refused_count < kMaxRefused ? aligned_alloc(0x1000, n) : nullptr;
    if (other) {
      first_refusal = refused_count ? first_refusal : rc;
      refused[refused_count++] = {block, n};
      block = other;
      continue;
    }
    ok = false;
    snprintf(g_pieced_note, sizeof(g_pieced_note),
             "svcMapMemory of piece %zu of %zu (%zu KB at %p) failed: 0x%X (module %u, description %u), %d pieces "
             "replaced before it; smallest piece %zu KB",
             p->mapped + 1, p->pieces.size(), n >> 10, block, rc, R_MODULE(rc), R_DESCRIPTION(rc), refused_count,
             SmallestPiece(p) >> 10);
  }
  virtmemUnlock();
  for (int i = 0; i < refused_count; ++i) {
    free(refused[i].first);
  }
  if (!ok) {
    UnmapPieced(p);
    return nullptr;
  }
  snprintf(g_pieced_note, sizeof(g_pieced_note),
           "%zu pieces (smallest %zu KB); %d refused by svcMapMemory and replaced (first 0x%X); %s",
           p->pieces.size(), SmallestPiece(p) >> 10, refused_count, first_refusal,
           reserve_fits ? "in the boot reservation"
           : g_reserve  ? "the boot reservation busy or too small: a range found now"
                        : "no boot reservation: a range found now");
  *handle = p;
  return p->view;
}

void RexSwitchFreePieced(void* handle) {
  if (handle) {
    UnmapPieced(static_cast<Pieced*>(handle));
  }
}

static constexpr uint64_t kStackPattern = 0xA5A5A5A5A5A5A5A5ull;

bool RexSwitchStackPrepare(uintptr_t* base, size_t* size) {
  *base = 0;
  *size = 0;
  const uintptr_t sp = uintptr_t(__builtin_frame_address(0));
  MemoryInfo info{};
  u32 page = 0;
  if (R_FAILED(svcQueryMemory(&info, &page, sp)) || !info.size || sp < info.addr ||
      sp >= info.addr + info.size) {
    return false;
  }
  const uintptr_t fill_end = (sp & ~uintptr_t(0xFFF)) - 0x4000;  // well below this frame and memset's own
  if (fill_end <= info.addr) {
    return false;
  }
  for (uint64_t* p = reinterpret_cast<uint64_t*>(info.addr); p < reinterpret_cast<uint64_t*>(fill_end); ++p) {
    *p = kStackPattern;
  }
  *base = info.addr;
  *size = info.size;
  return true;
}

size_t RexSwitchStackUsed(uintptr_t base, size_t size) {
  if (!base || !size) {
    return 0;
  }
  for (uintptr_t page = base; page < base + size; page += 0x1000) {
    if (*reinterpret_cast<const volatile uint64_t*>(page) != kStackPattern) {
      return base + size - page;
    }
  }
  return 0;
}

}  // extern "C"

#endif  /* REX_PLATFORM_SWITCH */
