#pragma once
#include <cstdint>

#include "transport.h"

// Milestone 5e: a per-machine game port (ini `game_port`, default 4200). Guests behind one NAT use distinct ports, so
// each router keeps its external port mapping and the NAT negotiation of the later guests no longer times out.
namespace game_port {

// Checks all sites, then writes them together (nothing when any differs): the bind port when `port` is not 4200, and
// with 3 or more players the advertised record ports, the same-public-IP quick check (the host stays on 4200, any
// other peer is reached at the port the host relayed) and the host's relay of each guest's local port (local_port_relay).
// `trace` adds the unmapped sender trace and logs the port and each decision. With the steam transport only that trace is
// installed (the port is ignored: every member is a virtual address at 4200). Run before the game socket is bound. True
// when the code was changed.
bool install(uint32_t port, uint32_t maxPlayers, bool trace, Transport transport);

}  // namespace game_port
