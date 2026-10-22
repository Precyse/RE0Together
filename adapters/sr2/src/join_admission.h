#pragma once
#include "partner_sites.h"
#include "partner_thunks.h"

// Milestone 9 (docs/SR2_NOTES.md, "Join admission"): the host admits several joiners one after the other.
//   N1a  the setup_request handler 0x831CA0: a request that comes while another admission is open is queued and answered
//        with "wait" (the answer that also clears the joiner's own deadline),
//   N1b  the member left callback closes the open admission only when the leaver is the member being admitted,
//   N1c  the co-op tick 0x8CB490 hands the next queued joiner to the handler once no admission is open.
namespace join_admission {

// With trace=1 each queued request and each replay is logged.
void setTrace(bool trace);

// The code behind N1a to N1c; {nullptr, nullptr} for a thunk of another file.
partner_thunks::Binding bind(partner_sites::Thunk thunk);

}  // namespace join_admission
