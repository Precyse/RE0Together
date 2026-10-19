#pragma once
#include "partner_sites.h"
#include "partner_thunks.h"
#include "vehicle_census_rules.h"

// Milestone 15 B1 (trace=1 only; docs/SR2_NOTES.md, "Traffic and the vehicle pool"): who spawns and despawns the ambient traffic
// and for which player. TT1 and TT2 hook the traffic list's add (0x938C30) and remove (0x938CB0), which every machine runs for its
// own traffic and for the traffic it receives; each event is counted by the vehicle's owner slot and the slot of the player it is
// nearest to, and one line a minute names the counts and the owned vehicles that live farther from their owner than from someone
// else. The vehicle part of the pool census line is read here too.
namespace traffic_trace {

// The vehicles in use by owner, the traffic and its quota, the net records and the free entries of the tables sized with the pool.
vehicle_census_rules::Census vehicleCensus();

// The code behind TT1 and TT2; {nullptr, nullptr} for a thunk of another file.
partner_thunks::Binding bind(partner_sites::Thunk thunk);

}  // namespace traffic_trace
