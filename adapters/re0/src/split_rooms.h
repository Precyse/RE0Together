#pragma once
#include <cstdint>

#include "door_sync.h"

// Players in different rooms at the same time (adapter.ini `split_rooms=1`).
//
// Both machines keep the same world model: every door either player takes is replayed on the other machine, but
// unless the two travel together (TEAM, same room) the replay leaves the local player where they are:
//   1. focus the door's character (a same-room swap, or the game's own zap when it is in another room),
//   2. run its door with the follow flag off, so the local character stays behind,
//   3. focus the local player's character again (swap or zap back).
// Apart, each machine keeps the camera on its own player's character, and enemies are run by whoever is in their
// room (the host again once together). A reunion is the same replay: the arriving character walks in through its
// door, and the game itself places both characters.
namespace split_rooms {

// Reads the setting. Call once at startup.
void configure(bool enabled);

// Game thread, from door_sync: a peer's door. True when split_rooms replays it (the caller must not run it).
bool takeOver(const door_sync::DoorChange& change);

// The local and peer rooms differ, or a door replay runs (and the feature is on): the camera stays on the local
// player's character and each machine runs its own enemies.
bool apart();

// A door replay is in progress: the follow flag stays off and the loaded room is not this player's, so room and
// player state are not reported.
bool replaying();

// Enemies here are simulated by this machine: the host when together, either machine when apart.
bool localEnemyAuthority();

// Registers the per-frame replay steps.
void enable();

}  // namespace split_rooms
