#pragma once
// The host's saves reach the guests: after the game saved (the profile's post-save hooks note it) and its SaveService has
// been idle for a moment, SAVE_CHANGED goes to the host's own launcher, which re-sends the profile's save files to every
// peer (docs/CONTRACT.md), so a Continue after a game over loads the same world on every machine.
#include <cstdint>

namespace save_notice {

// Any thread: the game just built a save.
void onSaved();

// Game thread, every step: sends SAVE_CHANGED for a save once it was written (host only, in a session).
void tick(uint64_t nowMs);

}  // namespace save_notice
