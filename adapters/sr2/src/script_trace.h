#pragma once
#include "partner_sites.h"
#include "partner_thunks.h"
#include "script_trace_rules.h"

// Milestone 13 (docs/SR2_NOTES.md, "Script errors and fades"), trace=1 only, read-only: a Lua thread that dies is dropped
// by the game without a message, so the thread runner's lua_resume (CE1) and the two load error alerts (CE2) log the
// error text; the script fade natives (FD1, FD2) and the fade tick (FD3) log what the fades do.
namespace script_trace {

// The screen fade as it is now.
script_trace_rules::FadeSnapshot readFade();

// The code behind CE1, CE2 and FD1 to FD3; {nullptr, nullptr} for a thunk of another file.
partner_thunks::Binding bind(partner_sites::Thunk thunk);

}  // namespace script_trace
