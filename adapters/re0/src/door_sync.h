#pragma once
#include <cstdint>

#include "net_client.h"

// Doors run on both machines together. Every door goes through sDoorLoad::start (door animation, then the room change);
// the machine that owns the focused character runs it and sends DOOR_CHANGE, the other machine runs the same call on
// receipt. A door the other
// machine would start from replayed input is suppressed, so the two games cannot pick different rooms. The game only
// lets the focused character act on doors, so a local player whose character is the partner acts through it: the
// trigger check is also run for that character, and when it acts the camera moves to it first.
namespace door_sync {

// Wire payload of DOOR_CHANGE (0x010B), reliable, to all: the arguments of sDoorLoad::start and the character that
// went through, which becomes the focused character on both machines.
struct DoorChange {
    uint32_t room;
    uint32_t entry;
    uint32_t arg3;
    uint32_t arg4;
    uint32_t flag;
    uint8_t characterId;
    uint8_t reserved[3];
};
static_assert(sizeof(DoorChange) == 24);

// Net thread: queues a peer's door for the game thread.
void onFrame(const GameFrame& frame);

// The door that brought this machine into its current room (run locally or from the peer), if any since load.
bool lastDoor(DoorChange& out);

// Queues `change` to run on the game thread as if the peer had sent it. A join teleport passes `bothTravel` so the
// local character goes along even when split_rooms would otherwise replay the door alone.
void queue(const DoorChange& change, bool bothTravel = false);

// Game thread: starts `change` here (the door's character must already be the camera character).
void run(const DoorChange& change);

// Hooks sDoorLoad::start and the act-on-trigger check and registers the per-frame apply. False when a hook cannot be installed.
bool enable(NetClient& net);

void uninstall();

}  // namespace door_sync
