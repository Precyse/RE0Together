#pragma once
#include <cstdint>

#include "control_rule.h"
#include "net_client.h"

// Where each player is: ROOM_STATE carries this machine's loaded scene (scene.h) on every arrival and every 2 s, and
// the peer's report answers "is the other player here". Also owns the enemy claim of a room: the machine that was in
// a room first keeps running its enemies when the other player walks in.
namespace door_travel {

// Wire payload of ROOM_STATE (0x0104), reliable, to all.
struct RoomState {
    uint16_t scene;         // the sender's loaded scene id (scene::kNone while loading)
    uint8_t partnerInRoom;  // the sender's partner character is in its loaded room
    uint8_t enemyClaim;     // the sender was in this room before the other player
    uint16_t doorTarget;    // the scene the sender's running door leads to, scene::kNone without a door (room_gate)
    uint8_t reserved[2];
};
static_assert(sizeof(RoomState) == 8);

using control_rule::PeerPlace;

// Net thread: remembers the driving peer's latest ROOM_STATE.
void onFrame(const GameFrame& frame);

// The driving peer's latest ROOM_STATE; false before the first one.
bool peerReport(RoomState& out);

// The peer's last reported room compared with the room loaded here (Unknown before the first report or while either
// side is loading).
PeerPlace peerPlace();

// This machine runs the enemies of its loaded room (control_rule::runsEnemies).
bool enemyAuthority();

// Net thread: notes a door start even when the game stops ticking, and tells the peer where the door leads.
void onNetTick();

// Registers the per-frame door watcher: arrival broadcasts ROOM_STATE and forces a position check, ROOM_STATE repeats
// every 2 s, and a lasting room mismatch is reported. The partner itself travels by the game's follow logic.
void enable(NetClient& net);

}  // namespace door_travel
