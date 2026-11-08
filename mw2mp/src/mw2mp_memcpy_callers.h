#pragma once

namespace mw2mp::memcpy_callers {

// Test build only (MW2MP_MEMCPY_CALLERS, mw2mp_memcpy_callers.cpp): the calling thread's memcpy, memmove, memset and
// memcmp calls are counted by call site from now on (one marked thread: the native renderer's ring thread).
void MarkThread();
// From the marked thread, every 10 s: the call sites with the most calls and bytes to the log. Does nothing in other
// builds.
void Report();

}  // namespace mw2mp::memcpy_callers
