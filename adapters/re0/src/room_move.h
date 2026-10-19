#pragma once
#include <cstdint>

// Native rooms in partner_mode=state (docs/RE0_NOTES.md "State mode: native rooms"). Each machine runs its own character
// through the engine's own doors; the other character is a puppet that lives in a room record of its owner's room, a
// dormant one when that is not the loaded room. The owner announces every room change of its own character with
// ROOM_MOVE (the commit point of its door, and any script or join placement) and the receiver moves the puppet into that
// room's record with `scene::move` at once. A pose room that disagrees with the puppet's record for a second, with no
// ROOM_MOVE, moves the puppet the same way (self-heal).
namespace room_move {

// Registers ROOM_MOVE with the frame router (policy Any: the sender must own the character it names), hooks
// sRoomControl::changeRoom and registers the per-tick placement watch, apply and self-heal. State mode only
// (state_mode::enableAll). False when the hook could not be installed.
bool enable();

void uninstall();

}  // namespace room_move
