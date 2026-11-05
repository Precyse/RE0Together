#pragma once
// The local Arisen and main pawn as the other players see them: one BODY_STATE per body at 30 Hz in plain gameplay
// (4 Hz with the held flag in a menu, load or travel, so a paused sender is not a lost one), ACTION_EVENT on every
// change of a layer's action.
#include <cstdint>
#include <optional>

#include "local_body.h"

namespace owner_stream {

// Game thread, every 30 Hz step while the session is active.
void tick(uint64_t nowMs);

// The last sample taken, for distances to the peers.
std::optional<local_body::Sample> lastSample();

}  // namespace owner_stream
