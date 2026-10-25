#pragma once
#include <cstdint>
#include <optional>
#include <vector>

#include "net_client.h"
#include "room_authority_wire.h"

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

// Any thread: the rooms this machine owns, as the CLAIMs it would send (what JOIN_EXTRAS carries to a joiner).
std::vector<room_authority_wire::AuthMessage> claims();

// Any thread: a hash of the whole table, (scene, owner, epoch) of every owned room in scene order; equal on both machines
// when their tables agree (what the world digest compares).
uint32_t digest();

// Any thread: claims `sender` made, handed over in a batch (JOIN_EXTRAS), judged by the table like the CLAIM messages.
void applyClaims(uint8_t sender, const std::vector<room_authority_wire::AuthMessage>& batch);

// This machine runs the enemies of `sceneId`. True while there is no linked session (the game plays as vanilla).
bool runsHere(uint16_t sceneId);

// The slot that runs `sceneId` and the table's epoch for it; `here` = that slot is this machine.
struct Holder {
    uint8_t slot;
    uint32_t epoch;
    bool here;
};

// Who runs `sceneId`, or nothing while there is no linked session or the room is unowned. Any thread.
std::optional<Holder> holderOf(uint16_t sceneId);

}  // namespace room_authority
