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

// Current room phase (room_phase.h), or room_phase::kUnreadable.
int32_t roomPhase();

// True while a local menu, map, message, save screen or cutscene holds the other player's world (room_phase.h).
bool uiPausesWorld();

// True in plain gameplay: Main phase, no door running, no menu open. Peer doors, placements and the party commands
// wait for it.
bool playing();

// Loaded room as stage << 8 | room; kRoomLoading while a room is still being loaded.
constexpr uint16_t kRoomLoading = 0xffff;
uint16_t currentRoom();

// True when the character is in the loaded room: its room record is the loaded one and the game shows it there. The
// in-room flag alone stays stale on a character moved into a dormant room (it does not update there).
bool inCurrentRoom(uintptr_t player);

}  // namespace game_state
