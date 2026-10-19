#pragma once
#include <cstdint>

#include "net_client.h"

// Who simulates the enemies of each room in partner_mode=state: one record per scene id in DS2's AuthorityTable, kept
// equal on both machines by CLAIM / STOP messages (room_authority_wire.h, rules room_authority_rule.h). The first
// machine whose own character stands in the loaded room claims it, the other becomes an observer; two arrivals at once
// settle on the lower slot on both machines; leaving the room releases it, a peer that goes away frees its rooms.
namespace room_authority {

// Registers the three message handlers and the per-tick claim / release. State mode only (state_mode::enableAll).
void enable();

// Net thread, every tick: follows the session (a new session forgets every record, a peer that left frees its rooms,
// a new peer is sent this machine's claims).
void onSession(const SessionSnapshot& session);

// Any thread: the claims of this machine are sent again (a joiner or a resync lost them).
void resendClaims();

// This machine runs the enemies of `sceneId`. True while there is no linked session (the game plays as vanilla).
bool runsHere(uint16_t sceneId);

}  // namespace room_authority
