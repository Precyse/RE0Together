#pragma once
#include <cstdint>

#include "character_owner.h"
#include "door_sync.h"
#include "flag_diff.h"
#include "inventory_sync.h"
#include "net_client.h"

// Joining a game in progress: the guest loads the host's last save, which can be older than the host's live game.
// Once the guest's game is loaded it asks for a snapshot; the host answers with where each character is (room, the
// last door it took, position), both inventories and the story flags. The guest takes its own character through that
// door if it is elsewhere (a normal door on its screen), moves the host's character into its room without touching
// the screen (scene::move), and places its own character where the host last saw it.
namespace join_sync {

// Where one character is on the host.
struct CharacterPlace {
    uint8_t hasDoor;
    uint8_t reserved;
    uint16_t scene;            // its room's scene id
    door_sync::DoorChange door;  // the last door it went through
    float pos[3];
    float quat[4];
};
static_assert(sizeof(CharacterPlace) == 4 + 24 + 28);

// Wire payload of JOIN_SNAPSHOT (0x010E), reliable, host to all. SNAPSHOT_REQUEST (0x010D) is u16 guest scene.
struct JoinSnapshot {
    CharacterPlace places[character_owner::kCharacterCount];
    inventory_sync::InventoryBlock inventories[character_owner::kCharacterCount];
    flag_diff::Words flags;
};
static_assert(sizeof(JoinSnapshot) == 2 * 56 + 128 + flag_diff::kWords * 4);

// True on the host, and on a guest once the host's snapshot is applied and its character stands where the host last
// saw it. Until then the guest keeps its own character's state (inventory, position) to itself: it comes from an
// older save.
bool caughtUp();

// Net thread: SNAPSHOT_REQUEST on the host, JOIN_SNAPSHOT on the guest.
void onFrame(const GameFrame& frame);

// Registers the per-frame request, answer and apply.
void enable(NetClient& net);

}  // namespace join_sync
