#pragma once
#include "partner_sites.h"
#include "partner_thunks.h"

// Milestone 17 wave 3 (docs/SR2_NOTES.md, "Zombie Uprising for 3 to 4 players (milestone 17 wave 3)"): the Zombie Uprising activity sets up,
// scores, bounds, prompts for and spawns zombies around every remote player human, where the game's code did it for the default partner.
// Sites: zombie_uprising_sites.h. Only installed with 3 or more players and the activity logic in place.
namespace zombie_uprising {

// The code behind a zombie site: its own stubs, then GB1's box test (store_sync) and MF17's name list (mission_flow); {nullptr, nullptr}
// for a thunk of another file.
partner_thunks::Binding bind(partner_sites::Thunk thunk);

// Writes the five sets (start and end, wave bonus, trigger zones, waiting prompt, spawn tests), each all or nothing, and logs each result.
void install(bool trace);

}  // namespace zombie_uprising
