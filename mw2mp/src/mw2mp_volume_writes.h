// The model lighting volume's writer, hooked: which pages the game wrote (mw2mp_native_volume_by_writes). See
// mw2mp_volume_writes.cpp.

#pragma once

#include <cstdint>
#include <vector>

namespace mw2mp::volume_writes {

// Whether UpdateVolume finds the changed pages from the writer's marks now (the setting, and not switched off by a
// recent miss).
bool UseMarks();

// Appends to `pages` the indices (0 = the page at physical_first) of the marked pages among [physical_first,
// physical_first + count * 4096) and clears their marks. physical_first is page-aligned.
void TakeMarks(uint32_t physical_first, uint32_t count, std::vector<uint32_t>& pages);

// A page changed that no mark covered (the check in UpdateVolume): marks are not used for 30 s, 60 s, 120 s after the
// first three misses, and for the rest of the run after the fourth. Logs the miss detail: runs = the page's changed
// 32-byte runs (bit n = offset n x 32), frames = since the check saw it changed.
void Missed(uint32_t physical_page, const uint64_t runs[2], uint64_t frames);

}  // namespace mw2mp::volume_writes
