#pragma once
#include <cstdint>

// Milestone 5i: a rejoining player showed another player's head and skin. The body part tints a player sends with its
// outfit come from tables indexed by the human's pool index, and only the boot clears them; a human created at an index
// that another player's copy used before sends that player's tints as its own. The tables of an index are cleared when a
// character instance is created for it.
namespace look_reset {

// Under trace=1 each cleared index is logged.
void setTrace(bool trace);

// A1: stands in for the call that creates a player human's character instance (cdecl: render object, pool index, is local).
uintptr_t resetIndexLooksThunk();

// T1 (trace=1): stands in for the call that packs the local human's tints into a components message; logs what it packs.
uintptr_t traceTintSendThunk();

}  // namespace look_reset
