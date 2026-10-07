#pragma once
#include <cstdint>

#include "net_client.h"

// Doors run on both machines together. Every door goes through sDoorLoad::start (door animation, then the room change);
// the machine that owns the focused character runs it and sends DOOR_CHANGE, the other machine runs the same call on
// receipt (or hands it to split_rooms, which moves the peer's character alone). A door the other machine would start from replayed input is suppressed, so the
// two games cannot pick different rooms. The game only lets the focused character act on doors, so a local player whose
// character is the partner acts through it: the trigger check is also run for that character, and when it acts the
// camera moves to it first.
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
    uint8_t flags;  // kDoorAlone
    uint8_t reserved[2];
};
static_assert(sizeof(DoorChange) == 24);

// A room script left the partner behind (TraceOff) before this door: only the character going through travels, whatever
// the party mode (a lift that takes one character).
constexpr uint8_t kDoorAlone = 1;

// Game thread: while set, a local door comes from a room script's door op that left the partner behind (event_sync).
void setScriptDoorAlone(bool alone);

// Net thread: queues a peer's door for the game thread.
void onFrame(const GameFrame& frame);

// The last door each character went through (either machine's), if any since load.
bool lastDoor(uint8_t characterId, DoorChange& out);

// Records `change` as the last door of its character.
void remember(const DoorChange& change);

// Queues `change` to run on the game thread as if the peer had sent it. A join teleport passes `bothTravel` so the
// door runs here even when split_rooms would otherwise move the peer's character alone.
void queue(const DoorChange& change, bool bothTravel = false);

// Game thread: starts `change` here (the door's character must already be the camera character).
void run(const DoorChange& change);

// Any thread: the door running here is played on both machines (a TEAM door taken together: this machine's, sent while
// together, or the peer's run here). A join teleport's door is not.
bool sharedDoor();

// Hooks sDoorLoad::start and the act-on-trigger check and registers the per-frame apply. False when a hook cannot be installed.
bool enable(NetClient& net);

void uninstall();

}  // namespace door_sync
