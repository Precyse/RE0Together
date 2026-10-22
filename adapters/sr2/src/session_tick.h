#pragma once
#include "partner_sites.h"
#include "partner_thunks.h"

// ST1, the session tick 0x8105B0 (every machine, every frame of a session; it times members out through 0x7FA9E0): before the
// game's tick, the stalled peer watch (peer_stall.cpp) and the guest's autosave (coop_autosave.cpp) run on the game thread.
namespace session_tick {

// The code behind ST1; {nullptr, nullptr} for a thunk of another file.
partner_thunks::Binding bind(partner_sites::Thunk thunk);

}  // namespace session_tick
