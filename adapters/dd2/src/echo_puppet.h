#pragma once
// Probe P4-1 (adapter.ini echo_puppet=1, off by default): once per game run, 10 s into plain gameplay, a copy of the
// local Arisen is built 3 m in front of the player exactly as a peer's would be: the Arisen's look contexts through the
// engine's JSON serializer and back (look.h), then the stray-pawn route (puppet_builder.h). Every step is logged; the
// first failure stops the probe; the copy is removed again after a minute. One PC, no peer needed.
#include <cstdint>

namespace echo_puppet {

void setEnabled(bool on);

// Game thread, every 30 Hz step.
void tick(uint64_t nowMs);

}  // namespace echo_puppet
