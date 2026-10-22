#pragma once
#include <cstdint>

#include "door_sync.h"

// TEAM doors in partner_mode=state (docs/RE0_NOTES.md "State mode: native rooms"). Nobody is carried: the machine whose
// character goes through a door asks the partner's owner, with DOOR_FOLLOW, to go through the same door with its own
// character; the owner judges the request against its own state and answers DOOR_FOLLOW_RESULT. Accepting means running
// the engine's own `sDoorLoad::start` with the runner's arguments and, once the door's room change commits, standing
// behind the character that came through. A rejected or unanswered request leaves the partner where it is.
namespace door_follow {

// Registers DOOR_FOLLOW and DOOR_FOLLOW_RESULT with the frame router (policy Any, the sender must own the character it
// names) and the per-tick judge. State mode only (state_mode::enableAll). False when a type could not be registered.
bool enable();

// Game thread, from the door start hook right after the local door started: asks the partner's owner to follow when the
// rules say so (shouldRequest).
void onLocalDoorStart(const door_sync::DoorChange& change);

// Game thread, from the room change hook once the own character's door committed into `sceneId` at `entry`: when this
// door was a followed one the character is put behind the leader. The entry spot mode it stands on afterwards.
uint8_t onOwnRoomChange(uint16_t sceneId, uint32_t entry);

}  // namespace door_follow
