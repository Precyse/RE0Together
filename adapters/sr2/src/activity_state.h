#pragma once
#include <cstddef>
#include <cstdint>

#include "activity_rules.h"

// Reads of the running activity and the session role, for the milestone 7 thunks.
namespace activity_state {

// With trace=1 the milestone 7 thunks log their decisions.
void setTracing(bool tracing);
bool tracing();

// True when this machine hosts the co-op session.
bool isHost();

// The type id of the running activity (docs/SR2_NOTES.md "Activities"), -1 when no activity is running.
int32_t runningActivityType();

// The member a message came from: [ctx+4] of the context a message handler gets, 0 when there is none.
uintptr_t senderOf(uintptr_t context);

// The remote members that are in the game, written to `members`; their count.
size_t remoteMembersInGame(uintptr_t (&members)[activity_rules::kMaxMembers]);

// The sub id of a race_req message the game has built (the byte after the header; one later when the flag byte is set).
uint8_t raceRequestSub(uintptr_t message);

// The member that invited this machine to a race and waits for the answer, 0 when none. The answer is sent from a
// dialog, outside the message handler, so the member is remembered here.
void setRacePeer(uintptr_t member);
uintptr_t takeRacePeer();

}  // namespace activity_state
