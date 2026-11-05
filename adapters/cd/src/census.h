#pragma once
// The funnel census line (probe L01): once a minute while the totals change, packet counts per direction, the
// counters that must stay 0, and the busiest opcodes by name.
#include <cstdint>

namespace census {

// Main tick only.
void tick(uint64_t nowMs);

}  // namespace census
