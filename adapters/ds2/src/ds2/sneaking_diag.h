#pragma once
// DS2-internal: log-only instruments for the NPC managers' activity update (see ds2/sneaking_diag.cpp).
#include <cstdint>

namespace sneaking_diag {

// Every 5 s: each group's members, sleepers, radii, limit and budget, and the members near the partner. Simulation thread.
void logGroups(uintptr_t manager);

}  // namespace sneaking_diag
