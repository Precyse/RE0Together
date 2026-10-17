#pragma once
#include <cstdint>

#include "partner_sites.h"
#include "partner_thunks.h"

// Milestone 9 MC1 and MC2, milestone 11 B4 (docs/SR2_NOTES.md, "Cutscenes"): the cutscene handshake waits for every remote.
//   MC1  the cutscn_pkt handler 0x6BECA0 keeps the ready, skip and done bytes clear until every remote member in the game
//        has sent that message; the start it runs for the host's cutscene (sub 0) is marked, so that
//   MC2  a cutscene that starts here forgets who has sent skip and done, and who has sent ready unless it is that start (the
//        game keeps its ready byte there, so an early ready survives),
//   CS1  the playback start and
//   CS2  the wait that gives up clear the game's ready byte, and the ready set with it.
// A member that leaves is handled by memberLeft, which the member left dispatcher (member_left.h) calls.
namespace cutscene_sync {

// A member left (the member left callback, every machine): it is dropped from the handshakes, and a handshake that waited
// only for it is completed the way the game's handler would have (its bytes are set), while a cutscene runs.
void memberLeft(uintptr_t leaver);

// The code behind MC1, MC2, CS1 and CS2; {nullptr, nullptr} for a thunk of another file.
partner_thunks::Binding bind(partner_sites::Thunk thunk);

}  // namespace cutscene_sync
