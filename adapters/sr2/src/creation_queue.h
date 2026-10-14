#pragma once
#include <cstdint>

// A session member's creation queue: the handles of the objects whose creation is yet to be sent to it (member +0x1DF0,
// count at +0x21F0). The game's own walk 0x8051F0 appends to it the same way; G1 reads the count for its trace, the leave
// path (LV3) appends single records.
namespace creation_queue {

// How many creations are queued for `member`.
uint32_t queued(uintptr_t member);

// Queues the creation of `record` for `member`: appends its handle and clears the member's bit in the record's created
// mask. False when the queue is full.
bool enqueue(uintptr_t member, uintptr_t record);

}  // namespace creation_queue
