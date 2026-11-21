// Derived from GPL-3.0 code; modified for bo2-zm-nx in 2026.
//
#pragma once
/*
 * Periodic log lines, written off the threads that do the real work.
 *
 * The log is synchronous (log_async = false): every few lines the file is flushed to the SD card, and the thread that
 * happens to be writing stalls for several milliseconds. The periodic reports add up to a couple of dozen lines every
 * 10 seconds from the game, audio and ring threads, so they are formatted on the thread that produces them and handed
 * to a dedicated "MW2 reports" thread (ReportDeferred in mw2mp_native_render.cpp) for writing. Warnings and errors are
 * still written immediately, from the thread that raises them.
 */
#include <string>

#include <rex/logging.h>

namespace mw2mp::native {
void ReportDeferred(std::string line);
}  // namespace mw2mp::native

// Not even formatted below info (the default log_level is warn).
#define MW2MP_REPORT_DEFERRED(...)                                                            \
  do {                                                                                        \
    auto* mw2mp_log_ = ::rex::GetLoggerRaw(::rex::log::core());                               \
    if (mw2mp_log_ && mw2mp_log_->should_log(spdlog::level::info))                            \
      ::mw2mp::native::ReportDeferred(fmt::format(__VA_ARGS__));                              \
  } while (0)
