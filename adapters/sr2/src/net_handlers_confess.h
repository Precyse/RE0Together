#pragma once
#include <cstdint>

#include "partner_sites.h"
#include "partner_thunks.h"

// Milestone 16 wave 2 (docs/SR2_NOTES.md, "Net handlers (M16 wave 2)"): the confessional, which tested "the partner" only.
//   CF1 to CF4  "the partner" is the remote farthest from the confessional for the too far status and the remote with the least
//               cash for the cannot pay status; the remotes that fail are recorded for the notice;
//   CF5         the host's notice goes to the recorded remotes, not to everyone;
//   CF8, CF9    a receiver of a guest's notice shows it only when it fails the test itself;
//   CF6, CF7    the lock "a remote is in a confessional" is the newest of a set of holders.
// With one remote every choice is that remote and every path is the shipped one.
namespace net_handlers_confess {

// The code behind the sites of the confessional; {nullptr, nullptr} for a thunk of another file.
partner_thunks::Binding bind(partner_sites::Thunk thunk);

// trace=1: one line per choice, notice and lock change.
void setTrace(bool trace);

// A member left: it is no longer recorded for a notice, and its lock is released.
void memberLeft(uintptr_t leaver);

}  // namespace net_handlers_confess
