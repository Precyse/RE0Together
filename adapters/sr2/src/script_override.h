#pragma once
#include <cstddef>

// Lua script overrides (milestone 7, AF9): a script the game loads by name comes from <game>\coop\scripts when a file of
// that name is there (tools/sr2/lua_overrides.py builds the stronghold ones from the game's own scripts).
namespace script_override {

// Lists <game>\coop\scripts once. Returns the number of files; a missing folder is no overrides. With `trace` each file
// is logged.
size_t scan(bool trace);

// The load of a script by name: `path` is the 0x400 byte buffer the game fills with the name it will open. When an
// override with that file name exists the buffer holds its path instead.
void redirect(char* path, size_t capacity);

}  // namespace script_override
