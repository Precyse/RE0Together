#pragma once
#include "partner_sites.h"
#include "partner_thunks.h"

// The exe edits of milestone 17 wave 3 (docs/SR2_NOTES.md, "Remote poses, world walks, garage door and client name (milestone 17 wave
// 3)"): the tagging and CPR poses, the ambient spawn view, the follower bars, the activity warp gate, the crib garage door, the
// Hitman view cone and {CLIENT_NAME} for 3 or more players. The tables are in world_walks_sites.h; each is one set, byte-checked
// and written all or nothing. Not part of multiplayer_sites::kPhases, so a set that differs never drops other logic.
namespace world_walks {

// The code behind a row of world_walks_sites.h: world_walks_thunks, else the identity thunks of partner_thunks_calls.cpp.
partner_thunks::Binding bindingOf(partner_sites::Thunk thunk);

// Writes the sets; a set whose bytes differ is left out. The activity warp gate waits for `activities` (the milestone 7 logic).
// `trace` turns on the thunks' trace lines. Each result is logged.
void install(bool trace, bool activities);

}  // namespace world_walks
