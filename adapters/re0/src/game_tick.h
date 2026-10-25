#pragma once

#include <cstddef>
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

// Any thread: true on the thread that runs the ticks (the game thread), false anywhere else and before the first tick.
// Recorders that fill game-thread-only buffers from engine calls the engine also makes off-thread check it first.
bool onGameThread();

// Any thread: the id of the thread that ran the last tick, 0 before the first.
uint32_t gameThreadId();

// Called with true just before and false just after each player's own move runs. Scopes enter in registration order
// and exit in reverse; at most kMaxMoveScopes. Safe to add while the hook is live (the game thread reads a published count).
// `name` picks the SLOW_FRAME column the scope's time is added to (slow_frame_rule.h) and names it when it raises an
// exception: that is logged, toasted and the scope (enter and exit) is disabled until the next resync, like a callback.
constexpr size_t kMaxMoveScopes = 4;
using MoveScope = void (*)(uintptr_t player, bool entering);
void addMoveScope(MoveScope scope, const char* name = "move_scope");

// Called after each player's own move has run, in registration order (game thread). Guarded like the callbacks: an
// exception is logged, toasted under `name` and the entry is disabled until the next resync. Call before install().
using PostMove = void (*)(uintptr_t player);
void addPostMove(const char* name, PostMove postMove);

// Hooks uPlayerBase::move. Call once, after the game code is decrypted.
bool install();

void uninstall();

}  // namespace game_tick
