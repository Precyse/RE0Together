#pragma once
#include <cstdint>

// Room-entry barrier (room_gate_rule.h): a machine that arrives in a room the peer's door is still taking it to holds
// its world (menu_mirror's sUnit::updateAll freeze) until the peer reports that room, turns away, or 15 s pass.
namespace room_gate {

// Game thread, from door_travel once a door's room is in place.
void onArrival(uint16_t scene);

// Game thread, every frame from sUnit::updateAll (which also runs while the world is held): true while the hold lasts;
// the frame that releases it logs why.
bool holdsWorld();

}  // namespace room_gate
