#pragma once
#include <cstdint>

namespace input_redirect {

// Hooks sGamePad::getPad and the analog getter so a remote-owned character's move reads the remote pad.
// Call after game_tick::install (MinHook must be initialised).
bool install();

void uninstall();

// True while a remote-owned character's move runs on this thread (its replayed input).
bool replayingRemoteInput();

// The game's own pad object for the local player, or null while input is blocked.
void* realPad(uint32_t index);

// The game's own analog block for the local player.
void* realAnalog();

}  // namespace input_redirect
