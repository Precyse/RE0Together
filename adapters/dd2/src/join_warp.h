#pragma once
// The join warp: after each load of ours and each load of the host's, a guest whose Arisen is more than 30 m from the host's
// newest body is warped to it (Character.warp), once per load pair, when both are in plain gameplay and the host's body
// state is from its current load. Guests only; needs puppet_driver::init (the warp option).
#include <cstdint>

namespace join_warp {

// Game thread, every 30 Hz step while the session is active.
void tick(uint64_t nowMs);

}  // namespace join_warp
