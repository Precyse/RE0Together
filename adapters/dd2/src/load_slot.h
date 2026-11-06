#pragma once
// Which save slot the game loaded: the slot argument of the SaveDataManager's one load funnel (names.h kRequestLoad),
// noted before the original runs. A game load names a slot of 0 or more (0 is the autosave); the system data load (-1) is
// not a game load and is ignored. Reported in GAME_STATE and compared with the host's by the overlay.
#include "game_state_rule.h"

namespace load_slot {

// Installs the load_slot hook set; false when it stayed off (logged).
bool install();

// Any thread: the slot of the newest game load, game_state_rule::kSlotUnknown before one.
int current();

}  // namespace load_slot
