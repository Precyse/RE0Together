#pragma once
#include <cstdint>

// Milestone 8: only peers running the same mod build play together (docs/SR2_NOTES.md, "Version gate"). Every join_req and
// join_acc ends with a trailer (version.h); the host refuses a join_req without a matching one (join_deny reason 1, the
// game's own "wrong game version"), and a guest refuses a join_acc without one the same way. On whenever the mod runs,
// at max_players 2 as well: an unmodded player is always refused. The browser name tag and the force join parts come with
// the milestone 6 merge.
namespace version_gate {

// Checks all sites, then writes them together (nothing when any differs). `maxPlayers` and `partnerExperimental` (the
// partner sites that are really patched) go into the trailer; `trace` logs each accept and refusal with its reason.
// Run before the trace hooks are enabled. True when installed.
bool install(uint32_t maxPlayers, bool partnerExperimental, bool trace);

}  // namespace version_gate
