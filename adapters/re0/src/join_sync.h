#pragma once
#include <cstdint>

#include "character_owner.h"
#include "door_sync.h"
#include "flag_diff.h"
#include "inventory_sync.h"
#include "net_client.h"

// Joining a game in progress: the guest loads the host's last save, which can be older than the host's live game.
// Once the guest's game is loaded it asks for a snapshot; the host answers with its room (and the door into it, which
// the guest runs as a teleport), both inventories (the host's AI Billy may have picked things up since the save),
// the story flags and Billy's position.
namespace join_sync {

// Wire payload of JOIN_SNAPSHOT (0x010E), reliable, host to all. SNAPSHOT_REQUEST (0x010D) is u16 guest room.
struct JoinSnapshot {
    uint16_t hostRoom;
    uint8_t hasDoor;
    uint8_t billyInRoom;
    door_sync::DoorChange door;
    float billyPos[3];
    float billyQuat[4];
    inventory_sync::InventoryBlock inventories[character_owner::kCharacterCount];
    flag_diff::Words flags;
};
static_assert(sizeof(JoinSnapshot) == 4 + 24 + 28 + 128 + flag_diff::kWords * 4);

// True on the host, and on a guest once the host's snapshot is applied and it stands in the host's room. Until then
// the guest keeps its own character's state (inventory, position) to itself: it comes from an older save.
bool caughtUp();

// Net thread: SNAPSHOT_REQUEST on the host, JOIN_SNAPSHOT on the guest.
void onFrame(const GameFrame& frame);

// Registers the per-frame request, answer and apply.
void enable(NetClient& net);

}  // namespace join_sync
