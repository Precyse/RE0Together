#pragma once
// Each peer's Arisen as a character in our world (on by default, adapter.ini peer_puppet=0 turns it off): built from the peer's BODY_CARD (look JSON back into contexts, look.h) through the stray-pawn route
// (puppet_builder.h) where its newest BODY_STATE puts it, while both machines are in plain gameplay. Rebuilt when the
// card changes, removed when the peer leaves or either side leaves plain gameplay. Once the engine reports the body
// ready it is driven from the owner's BODY_STATE and action edges (puppet_driver.h) with its own brain off
// (puppet_brain.h). Enabled only when those hooks and literals are in.
#include <cstdint>
#include <optional>

#include "puppet_registry.h"

namespace peer_puppet {

void setEnabled(bool on);

// Game thread: the bound body of a peer's Arisen or main pawn here, if shown.
std::optional<puppet_registry::Body> body(uint8_t slot, uint8_t kind);

// Game thread, every 30 Hz step.
void tick(uint64_t nowMs);

}  // namespace peer_puppet
