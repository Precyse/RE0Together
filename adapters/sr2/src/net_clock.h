#pragma once
#include "partner_sites.h"
#include "partner_thunks.h"

// Milestone 11 B1 (docs/SR2_NOTES.md, "Fast fire"). On player 3's screen the other players' guns and the NPCs' guns fired
// faster than their refire. All of them play their input history back against the session's net clock T (`[session+8]`),
// which the `clock` message handler sets from any sender, backward as well as forward; a step back replays the last
// milliseconds of every history and each replayed release and press re-arms a gun. On a guest with two or more remote
// members:
//   CK1  the clock message handler 0x7FCBF0 takes only the host's clock and never lets T step back (a clock behind T
//        becomes a debt, rules in net_clock_rules.h),
//   CK2  the net tick 0x7FC930 pays that debt by letting T advance at half speed.
// Everything else (the host, 2-player play) runs the game's own code.
namespace net_clock {

// With trace=1 every clock message is logged with the step it would make and the debt.
void setTrace(bool trace);

// The code behind CK1 and CK2; {nullptr, nullptr} for a thunk of another file.
partner_thunks::Binding bind(partner_sites::Thunk thunk);

}  // namespace net_clock
