#pragma once
// Crash containment for copies (cd design players.md 6.4, STUDY.md 8): a network-driven actor whose action layer has
// a chart but no current action or playback crashes a worker thread inside its character-control update. For an actor
// with one of our copy keys in that state, the update and post-update return at once; a copy is never spawned without
// these hooks. Real actors always run the original.
#include <cstdint>

namespace containment {

// Inside the hooks batch; false when either site's bytes differ.
bool install();

// After the hooks batch: true when both sites now jump to our detours. Until then active() is false.
bool confirmEnabled();

// True once both hooks are confirmed enabled: presence_recv spawns copies only then.
bool active();

// Updates skipped since start (update and post-update together).
uint64_t skipped();

}  // namespace containment
