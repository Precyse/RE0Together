#pragma once
#include "partner_sites.h"
#include "partner_thunks.h"

// Milestone 9 (docs/SR2_NOTES.md, "Sender scope"). Every message handler is called by one of two dispatchers, and in
// 2-player co-op the sender of a gameplay message is always the partner, so every `get_remote_player` call inside a
// handler means "the sender". SS1 (0x8138F0, the message dispatcher) and SS2 (0x813390, the in-order one) run the
// handler with the partner getters answering the member the message came from, so a message of player 3 or 4 is no
// longer taken for player 2's.
namespace sender_scope {

// With trace=1 the join messages the dispatcher sees are logged (milestone 5b T3), and a sender that has no human yet.
void setTrace(bool trace);

// The code behind SS1 and SS2; {nullptr, nullptr} for a thunk of another file.
partner_thunks::Binding bind(partner_sites::Thunk thunk);

}  // namespace sender_scope
