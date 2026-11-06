#pragma once
// The forced load from the title (adapter.ini auto_load, on for a co-op guest with a forced slot): once the title is up and idle
// (MainFlowManager in TitleMenu, the title GUI waiting, the SaveService idle, the save redirect safe) the plugin calls the game's
// own load, MainFlowManager.requestStartType(StartType.Latest), the call the menu's Continue ends in. No row is chosen and no
// menu handler runs; the load reaches SaveDataManager.requestLoad, where load_slot.h puts the host's slot in. The phases that
// follow tell whether it went through (title_load_rule.h); a failed try is repeated, then `load=failed` is reported.
// Evidence: docs/codemap/dd2.md "Title load".
#include <cstdint>

#include "game_state_rule.h"

namespace title_load {

// Installs the title_load hook set (it only watches the title GUI) and arms the load; false when it stayed off (logged).
bool install();

// Game thread, every step.
void tick(uint64_t nowMs);

// GAME_STATE load=: none until the load went through (ok) or was given up (failed).
game_state_rule::Load result();

}  // namespace title_load
