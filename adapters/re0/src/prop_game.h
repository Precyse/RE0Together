#pragma once
#include <cstdint>
#include <optional>

#include "prop_rule.h"

// The engine side of the push blocks (partner_mode=state, docs/RE0_NOTES.md "State mode: props"): finds a room's
// obstacle unit, tells whether the local character is pushing one and reads and writes a box's pose. Every access is
// exception-safe; game thread only.
namespace prop_game {

// The object of obstacle unit `unit` (a row of the engine's obstacle table) in room `scene`, or 0 when the room has no
// such box alive.
uintptr_t obstacleOf(uint16_t scene, uint8_t unit);

// The box a character is pushing right now (think group 0 state 9 with an obstacle in hand) and which unit of the room
// it is; nothing when the character is not pushing or the box is not one of the room's obstacle units.
struct Push {
    uint8_t unit;
    uintptr_t obstacle;
};
std::optional<Push> pushOf(uintptr_t player, uint16_t scene);

// The box's position and yaw.
bool readPose(uintptr_t obstacle, prop_rule::Pose& out);

// Puts the box where `pose` says: the position the push writes (and its fourth word 0), the rotation only when its yaw
// differs.
void writePose(uintptr_t obstacle, const prop_rule::Pose& pose);

}  // namespace prop_game
