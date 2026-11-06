#pragma once
// A small panel in the top left corner of the game, drawn through REFramework's ImGui (its on_imgui_frame callback) while
// co-op is on: each partner's name and state (title, loading, in world with the distance, or connection lost) and, below,
// one line per problem of this machine (game_state_rule::problems). Nothing is drawn when there is nothing to say.
//
// The panel text is built on the game thread (tick) and only copied by the draw, which runs on REFramework's render
// thread under a fault guard; a fault in the draw is logged and dumped, and the panel stays off.
#include <cstdint>

namespace overlay {

// Registers the draw callback; a guest compares its load slot with the one in the host's PLACE.
// False when REFramework refused the callback (logged).
bool install();

// Game thread, every step.
void tick(uint64_t nowMs);

}  // namespace overlay
