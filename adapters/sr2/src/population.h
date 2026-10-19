#pragma once
#include <cstdint>

#include "partner_sites.h"
#include "partner_thunks.h"
#include "population_rules.h"

// Milestone 9, the ambient population with 3 or more players (docs/SR2_NOTES.md, "Population"; rules in
// population_rules.h):
//   W1a  the area budget 0x93E8D0 follows the number of separate player areas, capped at the shipped maximum (the game's
//        own function answers with one remote, so 2 players are bit-exact),
//   W2a  the cluster rule 0x957A40: the lowest slot of a cluster spawns for it,
//   W10b the vehicle headroom 0x93C1D0 takes 4 off per remote session member and, with 2 or more remote members, keeps this
//        machine's owned traffic within its quota (milestone 15: the leader of each cluster of players owns its share of the
//        ambient vehicle cap).
namespace population {

// The player areas (the local one plus the soft count of the others, in hundredths) of the last W1a budget; 0 until one was
// evaluated, which needs two remote humans. The pool census (trace=1) reports it.
uint32_t lastAreasHundredths();

// The traffic quota of the last vehicle headroom evaluated with 2 or more remote members and the vehicle cap it shares out; all 0
// until then. The pool census (trace=1) reports it.
struct LastQuota {
    population_rules::Quota quota;
    int32_t limit = 0;
};
LastQuota lastQuota();

// trace=1 writes the quota line on every change of the quota or of the leader set.
void setTrace(bool trace);


// The code behind those rows; {nullptr, nullptr} for a thunk of another file.
partner_thunks::Binding bind(partner_sites::Thunk thunk);

}  // namespace population
