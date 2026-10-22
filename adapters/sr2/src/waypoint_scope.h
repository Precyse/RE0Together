#pragma once
#include "partner_sites.h"
#include "partner_thunks.h"

// Milestone 16 wave 2 (docs/SR2_NOTES.md, "HUD and waypoints (milestone 16 wave 2)"): a waypoint a script sets for one remote
// player reaches that player's machine alone. The script names the player through mission_debug (tools/sr2/lua/coop_waypoints.lua),
// and the three broadcasts of the waypoint senders go to that player's member while the name is set. Sites: hud_sites.h kWaypointScope.
namespace waypoint_scope {

// With trace on, every scoped send and each name that matches no remote is logged.
void setTrace(bool trace);

// The code behind GP1 to GP4; {nullptr, nullptr} for a thunk of another file.
partner_thunks::Binding bind(partner_sites::Thunk thunk);

}  // namespace waypoint_scope
