#pragma once
// DS2-internal: the partner's body is a second simulation focus on this machine (see ds2/partner_focus.cpp). The world
// around it stays active (enemies awake and fighting, physics, AI) the way it does around a second local player.
#include "decima/world_transform.h"

namespace partner_focus {

// Start-up: the detour on the engine's per-category activity update 0x140171d90. `diagnostics` (adapter.ini diagnostics=1)
// also logs the tables that keep members near the partner asleep and watches one sleeper's flags; never on in a shipped config.
void installEarly(bool diagnostics);

// The partner's body position while the body is live. Simulation thread.
bool partnerPosition(decima::WorldPosition& out);

double squaredDistance(const decima::WorldPosition& a, const decima::WorldPosition& b);

}  // namespace partner_focus
