#pragma once
#include <cstdint>

// The guest half of force join with the steam transport: the game's LAN browser finds the host's pong over the tunnel
// (docs/SR2_NOTES.md "Force join"). Reads the game, lets guest_join_rules decide, and carries the decision out through the
// game's own LAN functions. Front end main thread only.
namespace force_join_lan {

// One front end tick of a guest. `trace` logs each step.
void tick(uint64_t nowMs, bool trace);

// The request changed or went away: stops a search this module started (not in a session) and starts over.
void reset();

}  // namespace force_join_lan
