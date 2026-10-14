#pragma once
#include <cstdint>

#include "partner_sites.h"
#include "partner_thunks.h"

// A session member's creation queue: the handles of the objects whose creation is yet to be sent to it (member +0x1DF0,
// count at +0x21F0). The game appends to it from three places and never checks its capacity of 0x100: the entry after the
// last is the count itself, so a 257th object corrupts the member. Milestone 9 (W8) sends the handles that do not fit
// later: they wait on an overflow list and move into the queue after each drain, so there is no cap and 2 players, whose
// queue never fills, behave as shipped.
namespace creation_queue {

// How many creations are queued for `member`.
uint32_t queued(uintptr_t member);

// Appends a handle: into the queue while it has room, else onto the member's overflow list.
void append(uintptr_t member, uint32_t handle);

// Queues the creation of `record` for `member`: appends its handle and clears the member's bit in the record's created
// mask. False when the handle went to the overflow list.
bool enqueue(uintptr_t member, uintptr_t record);

// With trace=1 an overflow is logged.
void setTrace(bool trace);

// The code behind W8a to W8d; {nullptr, nullptr} for a thunk of another file.
partner_thunks::Binding bind(partner_sites::Thunk thunk);

}  // namespace creation_queue
