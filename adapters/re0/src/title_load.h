#pragma once
#include <cstdint>

#include "coop_menu_rule.h"
#include "save_load_rule.h"

// The session load from the title (docs/RE0_NOTES.md "Forced save load"): the host's Start on the Co-op page loads the
// picked slot and starts it the way the title's Load Game -> Continue does, then tells the guests (SESSION_LOAD); a
// guest at its title or on its Co-op page loads the same slot when the host's start arrives, and starts only when its
// copy hashes the same (it waits for a newer copy from the launcher otherwise). Every step is logged (`title_load:`).
// Game thread, driven from the title's move (title_coop.cpp owns the hook).
namespace title_load {

// Host: load and start `slot` once the Co-op page has closed.
void startHost(int32_t slot);

// The save the launcher picked (adapter.ini load_slot / load_hash): the host starts it by itself the first time its main
// menu is ready, the guests follow as they follow a Start. Call once at start-up.
void setLauncherPick(const save_load_rule::Order& pick);

// From the title's move, before and after the game's own step; `state` is uGUITitle +0x27c.
void titleBefore(uintptr_t title, uint32_t state);
void titleAfter(uint32_t stateBefore, uint32_t stateAfter);

// What the Co-op page shows of it.
coop_menu_rule::StartStatus status();

// A start is pending or running: the title is ours until it ends. Any thread.
bool busy();

// The host used Start this session, so a guest's title follows the host's starts (auto_join leaves the title alone).
// Any thread.
bool followsHost();

}  // namespace title_load
