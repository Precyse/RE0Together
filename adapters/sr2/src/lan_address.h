#pragma once
#include <cstdint>
#include <optional>

// Milestone 5g, L1: the local address the game reports to the other players is the LAN address of the real network
// adapter, not whatever Windows lists first for the host name (a VPN or virtual adapter often comes first, and a wrong
// private IP breaks the direct path between two players behind one router).
namespace lan_address {

// Checks the five gethostbyname call sites and retargets them together (nothing when any differs, or when `maxPlayers` is
// 2). `trace` logs the adapter and address that are chosen. True when the code was changed.
bool install(uint32_t maxPlayers, bool trace);

// The Steam tunnel writes the same five calls with its own table: they go to `thunk()`, which puts the virtual address
// `own()` returns (none before the launcher link is up) first in the list instead of choosing an adapter.
using OwnAddress = std::optional<uint32_t> (*)();
uintptr_t thunk();
void useVirtualRule(OwnAddress own);

}  // namespace lan_address
