#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

#include "anim_stream_rules.h"
#include "anim_trace_rules.h"

// Read-only readers of the game's streaming state for milestone 16 W1 (docs/SR2_NOTES.md "Anim and asset streaming per remote
// (milestone 16 W1)"): the need table, the pending requests, what is loaded, the stream entries of a slot and the trigger
// category byte. Every read is safe: a game that is not there reads as zero.
namespace anim_stream_state {

// The session slot of `member`, anim_stream_rules::kNoSlot for none.
uint8_t slotOfMember(uintptr_t member);

// The need table as the game's own code uses it (moved or not): from the base and end immediates of the code.
anim_trace_rules::NeedTableView needTableView();

// Every row of that table.
std::vector<anim_trace_rules::NeedRow> needRows();

// The requests this machine made and has no answer for.
std::vector<anim_stream_rules::Pending> pendingRequests();

// 0xA83340's two ways to know a group: loaded as a chunk, or the one the stream state is loading.
bool chunkLoaded(uint16_t group);
bool streamNames(uint16_t group);

// The stream entries as the other-sets list reads them (an entry in use only has its descriptor and group read).
std::vector<anim_trace_rules::StreamEntry> streamEntries();

// The slot descriptor of chunk anim slot `slot`.
inline uint32_t slotDescriptor(uint32_t slot) {
    return static_cast<uint32_t>(sr2::kAnimSlotDescriptors + slot * sr2::kAnimSlotDescriptorStride);
}

// The category byte of the game's own partner load (4, or 6 under Juiced).
uint8_t triggerLoadCategory();

}  // namespace anim_stream_state
