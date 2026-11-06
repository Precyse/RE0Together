#pragma once
// The other players on this screen (cd design players.md 2.2-2.3, 3, 9): each peer's template becomes a copy through
// two injected ActorLoadDataAck (keys, character number and position rewritten, template_rule.h) followed at once by its
// first action and a still Move, then its Move, StartAction and Rotate Reqs as Acks (presence_rule.h) on our own clock.
// Only while both machines are Ready and the containment hooks are in (containment.h), never inside a load window,
// only when this build's Req sizes match the recipes, and only when adapter.ini presence=1. A copy is removed with the game's own fade when the peer leaves, falls silent
// for 8 s (and does not come back until a frame arrives) or sends a new template generation; our own load clears every copy (the client drops them itself).
#include <cstdint>
#include <optional>
#include <span>

namespace presence_recv {

// After the catalogue: the Ack opcodes it injects. False when a name is missing.
bool resolveOpcodes();

void setEnabled(bool on);

// Main tick (link routing) for CHAR_* frames from a matching peer. False for a payload that does not decode.
bool onFrame(uint8_t slot, uint16_t type, std::span<const uint8_t> payload, uint64_t nowMs);

// Main tick. `slotsPresent` lists the peers the launcher still reports.
void tick(uint64_t nowMs, std::span<const uint8_t> slotsPresent);

// True while Acks may be injected for copies (enabled, injection on, recipes match, our world Ready).
bool canInject();

// A peer's copy as the modules that dress it see it; null when that peer has none.
struct CopyView {
    uint16_t generation = 0;   // the template it was spawned from
    uint32_t spawnCount = 0;   // bumps on every spawn: a respawn shows the template's state again
    bool live = false;         // spawned and past its login grace
};
std::optional<CopyView> copyView(uint8_t slot, uint64_t nowMs);

// Why a peer's copy is not shown while it should be (a game_state_rule.h code), or null: the template was refused, or the peer
// fell silent and its copy was removed. The first problem of any peer.
const char* copyProblem();

struct Stats {
    uint32_t templatesApplied = 0, movesApplied = 0, actionsApplied = 0, actionsHeld = 0;
};
Stats stats();

}  // namespace presence_recv
