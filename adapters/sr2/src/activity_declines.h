#pragma once
#include <cstdint>

// The decline tallies of the host's start wait (AF6) and zombie request (AC14), and the leave rule that completes them. A decline
// reply ends the request only once every remote member in the game has declined; a member that leaves without answering counts as
// a decline, and when the members that stay have all declined the game's handler runs for the leaver as it would have for an answer.
namespace activity_declines {

// AF6: `member` declined the start wait. True when every remote in the game has declined: run the game's decline handler.
bool startDeclined(uintptr_t member);

// AC14: `member` declined the zombie request `dialog`. True when every remote in the game has declined.
bool zombieDeclined(uint32_t dialog, uintptr_t member);

// The slot that holds the game's zombie answer handler (the AC14 trampoline), which the leave rule runs.
void** zombieAnswerOriginal();

// Called by the member left dispatcher (N4b) on every machine; acts on the host only.
void memberLeft(uintptr_t leaver);

}  // namespace activity_declines
