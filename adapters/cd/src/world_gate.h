#pragma once
// The local world gate fed from the funnels (world_gate_rule.h holds the decisions): which load bracket messages
// to watch, the own player's key and last position from its MoveActorReqs, and the CD_WORLD this machine reports.
#include <cstdint>

#include "funnels.h"
#include "wire_cd.h"
#include "world_gate_rule.h"

namespace world_gate {

// After the catalogue: asks the funnels for the load bracket and own-move events. False when a name is missing.
bool watchOpcodes();

// Main tick only.
void onEvent(const funnels::Event& event, uint64_t nowMs);
void tick(uint64_t nowMs);

world_gate_rule::State state();
wire_cd::World report();

}  // namespace world_gate
