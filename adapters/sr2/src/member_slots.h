#pragma once
#include <cstdint>

namespace member_slots {

// Reads the session's member list and logs it whenever a member, its session slot, its state or which one is local
// changes. A remote member whose session slot is outside 0..maxPlayers-1 would index the 8-peer deltacomp arrays out
// of range: that is logged and replication_switch puts co-op back on peer index 0.
void check(uint32_t maxPlayers);

}  // namespace member_slots
