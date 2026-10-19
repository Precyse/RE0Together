#pragma once
#include "partner_sites.h"
#include "partner_thunks.h"

// Milestone 16 wave 2 (docs/SR2_NOTES.md, "HUD and waypoints (milestone 16 wave 2)"): the minimap and the pause map draw one arrow
// per remote player by running the game's own partner arrow block once per remote, and the back prompt, the map distance and the
// follower blips know every remote. Sites: hud_sites.h. Only installed with 3 or more players and the partner sites in place.
namespace hud_remotes {

// With trace on, the thunks log the remotes of each pass, the back prompt holder and the nearest remote on change.
void setTrace(bool trace);

// The code behind HM1 to HM3, HP1, HP2, HQ1 to HQ3 and HT1; {nullptr, nullptr} for a thunk of another file.
partner_thunks::Binding bind(partner_sites::Thunk thunk);

}  // namespace hud_remotes
