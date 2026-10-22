#pragma once
#include <cstdint>

#include "partner_sites.h"
#include "partner_thunks.h"

// Milestone 9 N4, milestone 12 (docs/SR2_NOTES.md, "Pause flag"): the byte [0x252932F] that says "the partner paused" holds
// a bit per session slot. N4a (the 0x80 handler 0x8CB110) sets or clears the sender's bit; N4b (the member left callback
// 0x8CB260, its stub is member_left.cpp) clears the leaver's bit through `afterLeave`. Both call the game's apply 0x8CA190
// only when the byte goes between zero and not zero, so the pause count the apply keeps stays balanced.
namespace pause_mask {

// The leaver's bit is cleared, and the world is resumed when that was the last one pausing.
void afterLeave(uintptr_t leaver);

// trace=1: one line per message, leave and apply.
void setTrace(bool trace);

// The code behind N4a;
partner_thunks::Binding bind(partner_sites::Thunk thunk);

}  // namespace pause_mask
