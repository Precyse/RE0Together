#pragma once
// BODY_CARD (DD2 design adapter.md 5): the look of this player's Arisen and main pawn as the engine's own JSON of their
// look contexts (look.h), one card per body, sent when it changes (checked every 10 s in plain gameplay) and to every
// joiner; each peer's newest cards kept for the puppets that show them (peer_puppet.h).
#include <cstdint>
#include <optional>
#include <span>

#include "look.h"

namespace body_card {

// Game thread, every 30 Hz step.
void tick(uint64_t nowMs);

// Session routing. False for a payload that does not decode.
bool onFrame(uint8_t slot, std::span<const uint8_t> payload);

struct Card {
    uint32_t hash = 0;
    look::Json json;
};
// A peer's newest card of a body kind (wire_dd2 kKindArisen, kKindMainPawn), if it sent one in this session.
const Card* peerCard(uint8_t slot, uint8_t kind);

}  // namespace body_card
