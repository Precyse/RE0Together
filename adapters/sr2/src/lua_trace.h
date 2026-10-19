#pragma once
#include "partner_sites.h"
#include "partner_thunks.h"

// Milestone 16 wave 2 (trace=1 only; docs/SR2_NOTES.md, "Lua per-remote slots and LT1 (M16 wave 2)"): LT1 hooks the Lua native
// mission_debug (0xA398C0), which does nothing in the game, and writes the text of a call whose first argument starts with
// "coop: " to the log as `trace: lua: <text>`, so the per-remote Lua library (coop_slots.lua) can report which slot, seat or
// location a player got. The call itself runs as the game has it.
namespace lua_trace {

// Writes the LT1 hook (inside a hooks batch) when its bytes match; logs the result. Nothing is written when they differ.
void install();

// The code behind LT1; {nullptr, nullptr} for a thunk of another file.
partner_thunks::Binding bind(partner_sites::Thunk thunk);

}  // namespace lua_trace
