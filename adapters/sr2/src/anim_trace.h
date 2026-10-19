#pragma once
#include "partner_sites.h"
#include "partner_thunks.h"

// Milestone 14 bug 2 (trace=1, read-only; docs/SR2_NOTES.md, "Script errors and fades"): the crash at 0xCE5199 is the root motion
// decode (0xCE4870) reading an anim whose data lives in a chunk streamed anim slot that was released and reused. The traces tell
// whether two guests asking for the same anim set made the first one's release free it for the other:
//   AN1  the streaming_sync handler 0xA83F30 (message 0x7E: sub 0 request, 1 load, 2 "the remote no longer needs it") with the need
//        table 0x2229528 after it (one local byte and one remote byte per group: a 2 player assumption),
//   AN2  the slot unload 0xA841C0,
//   AN3  an anim whose header offset points past its data, before the decode that crashes on it.
namespace anim_trace {

// The code behind AN1, AN2 and AN3; {nullptr, nullptr} for a thunk of another file.
partner_thunks::Binding bind(partner_sites::Thunk thunk);

}  // namespace anim_trace
