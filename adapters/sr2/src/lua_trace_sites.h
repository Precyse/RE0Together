#pragma once
#include "partner_sites.h"

// Where milestone 16 wave 2 reads the Lua call of the mission scripts (docs/SR2_NOTES.md, "Lua per-remote slots and LT1 (M16 wave 2)").
// Copied from the research table of M16_W2_LUA.md; the bytes are what the exe has there. trace=1 only, installed with the activity phase
// (so only with 3 or more players).
namespace lua_trace_sites {

using partner_sites::Gate;
using partner_sites::Kind;
using partner_sites::Site;
using partner_sites::Thunk;

// LT1: the Lua native mission_debug, a no-op that ignores its arguments and returns the number 0. The first 6 bytes are
// `push ebp; mov ebp, esp; and esp, -8`, no relative operand.
inline constexpr Site kSites[] = {
    {"LT1", Kind::HookEntry, Gate::Now, 0xA398C0, "55 8B EC 83 E4 F8 8B 45 08 D9 EE", Thunk::TraceLuaDebug, ""},
};

}  // namespace lua_trace_sites
