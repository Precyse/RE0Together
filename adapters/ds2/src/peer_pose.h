#pragma once
// The partner's smoothed position. The simulation thread moves it toward each peer's extrapolated position every frame and
// hands it to the partner's body (remote_body::setTarget); the overlay draws the marker and the load where it is.
#include <cstdint>
#include <optional>

#include "world_to_screen.h"

namespace peer_pose {

// Start-up: registers the per-frame update on the simulation thread.
void installEarly();

// The position last computed for the peer in `slot`; empty until its first update. Any thread.
std::optional<world_to_screen::Vec3> shown(uint8_t slot);

}  // namespace peer_pose
