#pragma once
#include <cstdint>

#include "partner_sites.h"
#include "partner_thunks.h"

// Milestone 16 W1 phases C and D (anim_stream.h): the stream requests of every member and the set change time gates per sender.
//   SR1 to SR4  the four stream request message handlers (strm_req, strm_rem_req, osd_char_strm_req, osd_char_strm_rem_req) keep a
//               ledger per sender slot: an add is +1 and a remove -1. The game counts requests, so a member that leaves held
//               requests nothing released.
//   SC1, SC2    strm_set_change has two time gates per slot that every sender shared, so one sender's later time skipped another's
//               earlier set. Each sender slot has its own bank of the 18 gates; the game's gates hold the sender's bank while its
//               handler runs.
namespace anim_stream_requests {

// trace=1 logs the ledger and the set changes the shared gates would have skipped.
void setTrace(bool trace);

// What a leaver's requests released: the removes replayed through the game's world and character handlers.
struct Released {
    uint32_t world;
    uint32_t character;
};

// Releases every request `member` held by replaying its own remove messages through the game's handlers, then drops its ledger.
Released releaseLeaver(uintptr_t member);

// The code behind SR1 to SR4, SC1 and SC2; {nullptr, nullptr} for a thunk of another file.
partner_thunks::Binding bind(partner_sites::Thunk thunk);

}  // namespace anim_stream_requests
