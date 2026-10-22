#pragma once
#include <cstdint>

#include "join_file.h"

// The guest half of force join: reads the game, lets guest_join_rules decide, and carries the decision out through the
// game's own co-op browser (docs/SR2_NOTES.md "Force join"). Front end main thread only.
namespace force_join_guest {

// One front end tick of a guest with this request. `trace` logs each step.
void tick(const join_file::JoinRequest& request, uint64_t nowMs, bool trace);

// The request changed or went away: stops a query that is still running (not a joined session) and starts over.
void reset();

}  // namespace force_join_guest
