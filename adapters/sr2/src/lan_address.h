#pragma once
#include <cstdint>

// Milestone 5g, L1: the local address the game reports to the other players is the LAN address of the real network
// adapter, not whatever Windows lists first for the host name (a VPN or virtual adapter often comes first, and a wrong
// private IP breaks the direct path between two players behind one router).
namespace lan_address {

// Checks the five gethostbyname call sites and retargets them together (nothing when any differs, or when `maxPlayers` is
// 2). `trace` logs the adapter and address that are chosen. True when the code was changed.
bool install(uint32_t maxPlayers, bool trace);

}  // namespace lan_address
