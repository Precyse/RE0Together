#pragma once
#include "partner_sites.h"
#include "partner_thunks.h"

// Milestone 9 N4b, milestone 11 B4: the member left callback 0x8CB260 on every machine. Its prologue cleared the pause byte
// and resumed the world; N4b (the 21 bytes at 0x8CB28E, ebx = the leaver) is now one dispatcher that tells each part of the
// mod which member left: the pause flag (pause_mask::afterLeave, which also says whether the world resumes) and the cutscene
// handshake (cutscene_sync::memberLeft).
namespace member_left {

// The code behind N4b; {nullptr, nullptr} for a thunk of another file.
partner_thunks::Binding bind(partner_sites::Thunk thunk);

}  // namespace member_left
