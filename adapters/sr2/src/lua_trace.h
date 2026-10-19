#pragma once
#include <cstddef>

// Milestone 16 wave 2 (trace=1 only; docs/SR2_NOTES.md, "Lua per-remote slots and LT1 (M16 wave 2)"): the Lua scripts use the native
// mission_debug (0xA398C0), which does nothing in the game, as a log channel. waypoint_scope.cpp owns the one hook of that native and
// passes the text of each call here: a line the Lua library writes ("coop: <text>", coop_slots.lua) or the seat rules write
// ("coop seat ...", coop_seats.lua) is logged as `trace: lua: <text>`; any other text is the game's own and is ignored.
namespace lua_trace {

// Logs the first argument of a mission_debug call when it is one of the Lua library's lines. `length` is the number of readable
// characters of `text`, which ends at the first NUL.
void log(const char* text, size_t length);

}  // namespace lua_trace
