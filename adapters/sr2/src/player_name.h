#pragma once
#include <string>

// Milestone 12 (docs/SR2_NOTES.md "Player names"): the launcher's name for the local player, from the join file's `name`
// line. The game builds the local member's name in one function (0x80A070, edi = the wide destination, cdecl(cap)); the
// hook writes the launcher's name there instead of the game's own, and the game's saved names stay untouched.
namespace player_name {

// The join file's name (UTF-8, empty = none). A name outside the launcher's character set or over 15 characters counts as
// none. Called on the front end thread when the join file changes.
void set(const std::string& utf8);

// Checks the entry bytes and queues the hook (inside a hooks batch). A difference leaves the game's names.
bool install(bool trace);

}  // namespace player_name
