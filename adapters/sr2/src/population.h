#pragma once
#include "partner_sites.h"
#include "partner_thunks.h"

// Milestone 9, the ambient population with 3 or more players (docs/SR2_NOTES.md, "Population"; rules in
// population_rules.h):
//   W1a  the area budget 0x93E8D0 follows the number of separate player areas (the game's own function answers with one
//        remote, so 2 players are bit-exact),
//   W2a  the cluster rule 0x957A40: the lowest slot of a cluster spawns for it.
namespace population {


// The code behind those rows; {nullptr, nullptr} for a thunk of another file.
partner_thunks::Binding bind(partner_sites::Thunk thunk);

}  // namespace population
