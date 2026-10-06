#pragma once
#include <cstdint>

#include "door_sync.h"

// Players in different rooms, and independent play in LEAVE_BEHIND.
//
// The other player's doors never touch this machine's screen: unless the two travel together (TEAM, same room), the
// peer's character is moved straight into its new room's record (scene::move), exactly as the engine's own door
// carry moves a partner: into the loaded room it appears at the door, out of it it leaves, and between other rooms
// its dormant record follows it. The engine's own bookkeeping stays whole, so the save, a later reunion or a door
// into the peer's room all behave as in the vanilla game.
// While independent each machine keeps the camera on its own player's character; while apart each also runs its own
// room's enemies, and the first in a shared room keeps them (door_travel::enemyAuthority).
namespace split_rooms {

// Game thread, from door_sync: a peer's door. True when split_rooms applies it (the caller must not run it).
bool takeOver(const door_sync::DoorChange& change);

// Game thread, before a door this machine started loads `room`: when only the peer's character sits in a dormant
// record of that room (made by its door), the character steps into the loaded room and the record is freed, so the
// door loads the room fresh. A reused dormant record never ran the room's spawn, so its enemies and floor items
// would be missing.
void beforeLocalDoor(uint16_t room);

// Game thread, once the local door's room is loaded: puts a character taken out by beforeLocalDoor back into it.
void onArrival();

// The peer reports another room than the one loaded here, or this player's own character is outside the loaded room.
bool apart();

// LEAVE_BEHIND, or apart: each machine keeps its own player's character in focus (no shared camera).
bool independent();

// Enemies of the loaded room are simulated by this machine.
bool localEnemyAuthority();

// Registers the per-frame apply.
void enable();

}  // namespace split_rooms
