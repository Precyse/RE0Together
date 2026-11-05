#pragma once
// What each peer reports of its own bodies: the newest BODY_STATE and the action edges, mapped onto this machine's
// clock by the shared stream_clock (adapters/re0/src). The puppet driver reads it; until puppets exist a summary line
// every 30 s shows that the streams flow (rate, distance, last action).
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "wire_dd2.h"

namespace peer_bodies {

// Game thread (session routing). False for a payload that does not decode.
bool onBodyState(uint8_t slot, std::span<const uint8_t> payload, uint64_t nowMs);
bool onActionEvent(uint8_t slot, std::span<const uint8_t> payload);

// The newest state of a peer's body of a kind (wire_dd2 kKindArisen, kKindMainPawn), if any arrived in this session.
std::optional<wire_dd2::BodyState> latest(uint8_t slot, uint8_t kind);

// The action edges of that body since the last call, oldest first (at most kMaxPendingEdges are kept).
constexpr size_t kMaxPendingEdges = 32;
std::vector<wire_dd2::ActionEvent> takeActionEdges(uint8_t slot, uint8_t kind);

// Session: the peer on this slot left or was replaced; its stream starts over.
void forget(uint8_t slot);

// Game thread, every 30 Hz step: the summary line.
void tick(uint64_t nowMs);

}  // namespace peer_bodies
