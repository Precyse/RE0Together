#pragma once
#include <cstdint>

#include "partner_sites.h"
#include "partner_thunks.h"

// Milestone 16 W2 (docs/SR2_NOTES.md, "Mission flow (milestone 16 W2; MF1 to MF23)"). The mission start prompts, the replay prompt and
// the mission bonus counters are made for 3 and 4 players on top of the sender scope of milestone 9. With one remote member every
// thunk is the shipped behaviour; the sites are written only with 3 or more players (mission_flow_sites.h).
namespace mission_flow {

// With trace=1 the thunks log their decisions (prefix "trace: mission flow: ").
void setTrace(bool trace);
bool tracing();

// The session slot of a member, for the trace lines.
unsigned slotOf(uintptr_t member);

// The code behind MF1 to MF17 and MF23 (mission_flow_prompts.cpp, mission_flow_rewards.cpp); {nullptr, nullptr} for a thunk of
// another file.
partner_thunks::Binding bind(partner_sites::Thunk thunk);

// The code behind MF18 to MF20 and MF23 (mission_flow_rewards.cpp).
partner_thunks::Binding bindRewards(partner_sites::Thunk thunk);

// A remote member left: the start and replay requesters it was are forgotten, and its leave counts as a decline of the host's open
// replay wait (mission_flow_prompts.cpp).
void memberLeft(uintptr_t leaver);

}  // namespace mission_flow
