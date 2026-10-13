#pragma once
// Host: an order a guest accepted at its terminal is started in the host's world (story.cpp replays the guest's request), so
// its cargo appears on the host's side. Once the mission is in progress and its pieces exist outside every backpack, each
// piece is given to the guest with the ordinary give (CARGO_ADD with the order link). The guest's own replay of the start
// creates no cargo. A request whose order never starts is dropped after kHandOffWindow (docs/DS2_NOTES.md, "Guest orders").
#include <cstdint>

#include "net_client.h"

namespace order_handoff {

// Start-up: registers the simulation-thread scan.
void installEarly();

// Simulation thread, host: the guest's request for `missionId` was replayed here; its cargo is handed over once it exists.
void expect(uint64_t missionId);

// Net thread: gives the pieces the scan found to the guest.
void tick(NetClient& net, const SessionSnapshot& session);

}  // namespace order_handoff
