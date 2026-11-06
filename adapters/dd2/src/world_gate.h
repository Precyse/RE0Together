#pragma once
// The local world gate fed by the MainFlowManager and ferrystone hooks (world_gate_rule.h holds the decisions).
#include <cstdint>
#include <string>

#include "world_gate_rule.h"

namespace world_gate {

// Resolves the phase and scene type literals and installs the world_gate hook set; false when the set stayed off.
bool install();

// Any thread.
world_gate_rule::Place place();
uint16_t loadSerial();

// The newest MainFlowManager phase by name (the literal's name, "?" for a value the enum does not list), empty before the first.
std::string phase();

// The tick tells the gate which thread is the game's (rule T1: a hook on another thread only counts).
void setGameThread(unsigned long threadId);

}  // namespace world_gate
