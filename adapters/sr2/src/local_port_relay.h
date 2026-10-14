#pragma once
#include <cstdint>

// Milestone 5g: guests behind one home network reach each other at their local ports. The host remembers the local port
// each guest reports in its join_req (H1) and puts it into the address records it sends to the other guests (H2); T11
// logs unmapped senders from private address ranges (trace=1).
namespace local_port_relay {

// The code the sites of game_port_patches go to.
struct Thunks {
    uintptr_t reportedPort;       // H1: the join_req handler, before the reported port is overwritten with the real source
    uintptr_t relayedRecordPort;  // H2: the new_conn record builder
    uintptr_t unmappedSender;     // T11: the receive loop's sender lookup
};

Thunks thunks();

// Under trace=1 the decisions are logged.
void setTrace(bool trace);

}  // namespace local_port_relay
