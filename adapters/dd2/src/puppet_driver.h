#pragma once
// Drives a puppet's body from its owner's streams (DD2 design players.md 5, puppet side), inside a driver scope so
// the brain-off gates let it through: a warp when the body is over kWarpM off (or first), else the owner's discrete
// locomotion speed with the turn target on the owner's position (or along the owner's facing once close), and the
// owner's action edges replayed by hash; the body's AI decision module is held in think-skip every step. No render
// delay or blend yet: the newest sample is the target.
#include <cstdint>
#include <span>

#include "puppet_registry.h"
#include "wire_dd2.h"

namespace puppet_driver {

// Once at start: the locomotion literals. False when the enum is missing (driving then only warps).
bool init();

struct State {
    bool warped = false;
    int32_t lastSpeed = -1;
};

// Game thread, every 30 Hz step for a puppet whose body is ready.
void drive(const puppet_registry::Body& body, const wire_dd2::BodyState& target,
           std::span<const wire_dd2::ActionEvent> edges, State& state);

}  // namespace puppet_driver
