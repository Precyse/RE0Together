#pragma once
#include <cstdint>

// Read-only, exception-safe views of the running game's door, menu and room state.
namespace game_state {

// Raw sDoorLoad phase, or door_phase::kUnreadable.
int32_t doorPhase();

// True while a door transition runs (door_phase.h: phases 0..4; 5 and -1 are idle).
bool doorActive();

// True while an inventory, map or pause submenu is open.
bool menuOpen();

// Loaded room as stage << 8 | room.
uint16_t currentRoom();

// True when the character is in the loaded room (false for a partner left in another room).
bool inCurrentRoom(uintptr_t player);

}  // namespace game_state
