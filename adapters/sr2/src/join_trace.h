#pragma once
#include <cstdint>

namespace join_trace {

// Read-only logging of the session and join events of a co-op join, to learn their order and the limits in play, and
// the session members with their slots (member_slots) at those events. Calls that repeat are logged when their
// result changes.
void install(uint32_t maxPlayers);

}  // namespace join_trace
