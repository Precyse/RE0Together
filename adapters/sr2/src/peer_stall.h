#pragma once
#include "partner_sites.h"
#include "partner_thunks.h"

// Stalled peers (docs/SR2_NOTES.md, "Stalled peers and rejoins"; rules in peer_stall_rules.h). The game shows a peer whose game
// stopped frozen until its 60 s timeout, and the stopped peer itself reports a receive rate of 0 when it is asked first thing
// after it resumes.
//   tick  (the session tick, session_tick.cpp) a remote member that sent nothing for peer_stall_rules::kStallMs has its human
//         hidden here (0x926860, the cutscene's own hide), and shown again when it sends again;
//   PS2   the rate report 0x80AB90 sends nothing while 0x815120 has no measurement (0), so the senders keep their rate.
namespace peer_stall {

// On the game thread, at each session tick.
void tick();

// The code behind PS2; {nullptr, nullptr} for a thunk of another file.
partner_thunks::Binding bind(partner_sites::Thunk thunk);

}  // namespace peer_stall
