#pragma once
// The guest's save hold (cd design progression.md 5.2, rule save_hold_rule.h). While a replayed story branch waits for
// its answer, or our gate is Loading or AwaitLocalReady, the game's SaveAutoReq is held at PostPacket (the newest one
// kept, the real call told it was queued) and posted when the hold ends, so no autosave captures a half-applied replay.
// A hold lasts at most 30 s: then the waiting replays are abandoned and the save goes out. Saves the player asks for are
// never held (no shadows exist in this build). On only on a guest with story replay on.
#include <cstdint>

namespace save_guard {

// After the catalogue, before install: SaveAutoReq is the held opcode. False when the name is missing.
bool resolveOpcodes(bool enabled);

// Main tick.
void tick(uint64_t nowMs);

struct Stats {
    uint32_t holds = 0;      // holds begun
    uint32_t bounded = 0;    // holds that reached the 30 s bound
    uint32_t heldSaves = 0;  // autosaves held (and posted after)
};
Stats stats();

}  // namespace save_guard
