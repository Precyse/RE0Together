#pragma once
// Pings (PING_SET, ping_wire.h): each player's placed user markers go to the partner when they change and to a peer that joined;
// the partner's are drawn by the marker overlay. Presentation only: there is no host authority and nothing is written into the
// game.
#include <cstdint>
#include <vector>

#include "net_client.h"
#include "ping_wire.h"

namespace ping_sync {

// Net thread.
void onFrame(const GameFrame& frame);
void tick(NetClient& net, const SessionSnapshot& session);

// One of a partner's pings, with the slot it came from.
struct PartnerPing {
    uint8_t slot;
    ping_wire::Ping ping;
};

// Every ping the partners currently have placed. Render thread.
std::vector<PartnerPing> partnerPings();

}  // namespace ping_sync
