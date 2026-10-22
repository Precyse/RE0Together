#pragma once
#include <cstdint>

// Milestone 5d: players behind one NAT. The code the duplicate member search call of player_cap_patches goes to.
namespace home_network {

// Under trace=1 the decisions are logged.
void setTrace(bool trace);

// D1: stands in for the call to the duplicate member search of the host's join_req handler (eax = session, esi = sender
// block): the search's member counts only when it has the sender's real source address.
uintptr_t duplicateJoinSenderThunk();

}  // namespace home_network
