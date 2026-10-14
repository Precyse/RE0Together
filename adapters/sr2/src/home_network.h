#pragma once
#include <cstdint>

// Milestone 5d: players behind one NAT. The code the three call sites of player_cap_patches go to.
namespace home_network {

// Under trace=1 the decisions are logged.
void setTrace(bool trace);

// D1: stands in for the call to the duplicate member search of the host's join_req handler (eax = session, esi = sender
// block): the search's member counts only when it has the sender's real source address.
uintptr_t duplicateJoinSenderThunk();

// D2, D2b: stands in for the calls to gethostbyname that read the local address: the default route interface's IPv4 is
// first in the list, not whatever Windows lists first for the host name.
uintptr_t hostByNameThunk();

}  // namespace home_network
