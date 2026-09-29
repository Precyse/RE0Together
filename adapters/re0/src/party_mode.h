#pragma once
#include "control_rule.h"
#include "net_client.h"
#include "protocol.h"

// Shared party mode (TEAM or LEAVE_BEHIND). Any player's E press asks the host to flip it; the host announces the
// result to everyone, reliably and every 2 s.
namespace party_mode {

// The mode this machine currently applies (Team while no peer is present).
control_rule::PartyMode current();

// The local player pressed the party key (net thread). The host flips at once, a guest sends PARTY_REQUEST.
void onLocalToggleKey();

// Net thread: PARTY_REQUEST (host) and PARTY_MODE (guest).
void onFrame(const GameFrame& frame);

// Registers the per-frame flip, announcement and toast.
void enable(NetClient& net);

}  // namespace party_mode
