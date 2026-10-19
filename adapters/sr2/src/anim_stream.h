#pragma once
#include <cstdint>

#include "partner_sites.h"
#include "partner_thunks.h"

// Milestone 16 W1 (docs/SR2_NOTES.md "Anim and asset streaming per remote (milestone 16 W1)"): with 3 or 4 players the chunk streamed
// anim sets, the stream requests and the activity trigger character sets were shared by every remote as if there were one. The sites
// are six phases (anim_stream_sites.h), each checked and written all or nothing, and only with `max_players` above 2 and the
// milestone 9 sites in:
//   A  streaming_sync (message 0x7E) goes only to the machine it concerns and runs only where it makes sense (AS1 to AS5), the remote
//      byte of a need row is a mask with a bit per member (AM1 to AM3), a leaver's bits and requests are released (AL1),
//   B  the need table is sized for `max_players` on the first call of the level loader's table reset (AT0),
//   C  the stream requests of every member are counted (anim_stream_requests.cpp),
//   D  the set change time gates are kept per sender (anim_stream_requests.cpp),
//   E  the trigger character sets are kept per member (anim_stream_triggers.cpp),
//   F  the anim set load trace (trace=1).
// This file holds A and B and puts the phases together.
namespace anim_stream {

// Checks the bytes of every phase and logs each difference. True when at least one phase matches (the others are left out).
bool sitesMatch();

// Writes the phases that matched (the trace phase only with `trace`), inside a hooks batch; a phase whose sites cannot all be
// written is left out and logged.
void install(uint32_t maxPlayers, bool trace);

// The code behind the sites of the six phases; {nullptr, nullptr} for a thunk of another file.
partner_thunks::Binding bindingOf(partner_sites::Thunk thunk);

}  // namespace anim_stream
