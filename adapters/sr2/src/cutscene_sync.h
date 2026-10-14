#pragma once
#include "partner_sites.h"
#include "partner_thunks.h"

// Milestone 9 MC1 and MC2 (docs/SR2_NOTES.md, "Cutscenes"): the cutscene handshake waits for every remote.
//   MC1  the cutscn_pkt handler 0x6BECA0 keeps the ready, skip and done bytes clear until every remote member in the game
//        has sent that message,
//   MC2  a cutscene that starts here forgets who has sent what.
namespace cutscene_sync {

// The code behind MC1 and MC2; {nullptr, nullptr} for a thunk of another file.
partner_thunks::Binding bind(partner_sites::Thunk thunk);

}  // namespace cutscene_sync
