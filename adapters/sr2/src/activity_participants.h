#pragma once
#include <cstdint>

// Who the other participant of a two player activity is (docs/SR2_NOTES.md, "Diversions and activities (milestone 16 wave 2)",
// rule PIN). A race or an escort has two participants and spectators; the game asks the partner getter for "the other
// participant", which with 3 or 4 players is the default partner and not necessarily the one taking part.
namespace activity_participants {

// The remote player human the default partner must be now: the other one of the pair (mission owner, mission partner) while a
// 2+S activity or the co-op race diversion runs and the local player is one of the two. 0 when there is no pin. Reads game
// memory only, so the getter hook can ask it on every call.
uintptr_t pinnedHuman();

}  // namespace activity_participants
