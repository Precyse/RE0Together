#pragma once
#include <cstdint>

#include "partner_sites.h"
#include "partner_thunks.h"

// Milestone 9 N4 (docs/SR2_NOTES.md, "Pause flag"): the byte [0x252932F] that says "the partner paused" holds a bit per
// session slot. N4a (the store in the pausing message handler 0x8CB110) sets or clears the sender's bit; N4b (the member
// left callback 0x8CB260, its stub is member_left.cpp) clears the leaver's bit through `afterLeave` and ends the pause only
// when no bit is left.
namespace pause_mask {

// The leaver's bit is cleared. True when that was the last one pausing, so the world is to be resumed.
bool afterLeave(uintptr_t leaver);

// The code behind N4a;
partner_thunks::Binding bind(partner_sites::Thunk thunk);

}  // namespace pause_mask
