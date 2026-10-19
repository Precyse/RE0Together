#pragma once
#include <cstdint>

#include "partner_sites.h"
#include "partner_thunks.h"

// Milestone 16 wave 2 (docs/SR2_NOTES.md, "Net handlers (M16 wave 2)"): the net handlers that kept the state of one remote keep each
// remote's.
//   BW1 to BW5  the busted wait (misc_19 and the local bust): a deadline per remote member, the notoriety clears when the host and
//               every remote are waiting, a downed local bleeds out when every other remote is out;
//   RH1, RH2    the race resource set a remote holds: a bit per session slot, loaded by the first holder, unloaded by the last;
//   RP1 to RP4  the mission replay: the wait ends when every remote declined, the wait dialog names every remote, a guest takes the
//               replay prompt, answer and start only from the host;
//   SL1         the session lost dialog names the host's human on a guest.
// The confessional (CF1 to CF9) is net_handlers_confess.h. With one remote every path is the shipped one.
namespace net_handlers {

// The code behind a site of net_handlers_sites.h; {nullptr, nullptr} for a thunk of another file.
partner_thunks::Binding bind(partner_sites::Thunk thunk);

// Writes the sets of net_handlers_sites.h, each all or nothing (inside a hooks batch); logs which went in. `trace` adds the trace
// lines of the decisions.
void install(bool trace);

// A member left (the member left callback, ahead of its removal): its busted wait, race hold, replay answer and confessional lock go.
void memberLeft(uintptr_t leaver);

// The member whose message the calling thread is handling, else the default partner's member.
uintptr_t senderMember();

// The session slot of a member, partner_rules::kNoSlot for none.
uint8_t memberSlot(uintptr_t member);

}  // namespace net_handlers
