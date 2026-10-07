#pragma once
#include <cstdint>

// Door barrier (room_gate_rule.h). The door's last phase asks 0x551c70 whether the door may finish (its only caller);
// finishing ends the room phase DoorLoad and starts the room. For a door both machines play (TEAM, together), this
// machine reports that its door is ready, and while the peer's is not, the check says "not yet": the door stays on its
// last frame, the room and its enemies do not start, nothing is frozen on screen. Released when the peer's door is
// ready, the peer turns away, the link drops, or after 5 s; a hold over 2 s shows "Waiting for partner".
namespace room_gate {

// Hooks the door's finish check. False when the hook failed.
bool install();

// Any thread: this machine's running door is ready to finish (ROOM_STATE tells the peer).
bool doorReady();

}  // namespace room_gate
