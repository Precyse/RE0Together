#pragma once
// GAME_STATE (adapters/re0/src/protocol.h kGameState): what this game reports of itself to its own launcher, as the text of
// game_state_rule.h, sent when the link comes up and whenever a value changes. The overlay shows the same report.
#include <cstdint>
#include <optional>

#include "game_state_rule.h"

namespace game_state {

// The report now. None for the first seconds of a launch while the save redirect is still being set (it is neither
// verified nor refused yet), so a launch does not flash a refusal.
std::optional<game_state_rule::Report> current();

// Game thread, every step.
void tick(uint64_t nowMs);

}  // namespace game_state
