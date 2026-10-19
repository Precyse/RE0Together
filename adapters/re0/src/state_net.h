#pragma once
#include <cstdint>
#include <span>

#include "net_client.h"

// The only send path for partner_mode=state messages (types 0x0140..0x017f, state_net_rule.h). Outside state mode a
// state type is refused and counted, so input mode can never put one on the wire.
namespace state_net {

// Remembers the client the frames go through. Call once, before any send.
void bind(NetClient& net);

// Queues `payload` to every peer. False when refused (state type outside state mode) or while not linked.
bool send(uint16_t type, bool reliable, std::span<const uint8_t> payload);

}  // namespace state_net
