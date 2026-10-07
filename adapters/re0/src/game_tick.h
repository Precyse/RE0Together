#pragma once

#include <cstdint>

namespace game_tick {

using Callback = void (*)();

// Registers a per-frame callback run on the game thread before the controlled player's move. Call before install().
// A callback that raises an exception is logged (code, address, a minidump for the first one), toasted on screen and
// disabled until the next resync.
void addCallback(const char* name, Callback callback);

// Any thread, once a fresh join snapshot was sent (host) or applied (guest): disabled callbacks run again from the
// next frame. A fault that persists disables them again.
void rearmAfterResync();

// Called with true just before and false just after each player's own move runs.
using MoveScope = void (*)(uintptr_t player, bool entering);
void setMoveScope(MoveScope scope);

// Called after each player's own move has run (game thread, guarded like the callbacks).
using PostMove = void (*)(uintptr_t player);
void setPostMove(PostMove postMove);

// Hooks uPlayerBase::move. Call once, after the game code is decrypted.
bool install();

void uninstall();

}  // namespace game_tick
