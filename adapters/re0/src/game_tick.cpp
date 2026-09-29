#include "game_tick.h"

#include <windows.h>

#include <array>

#include "debug_stats.h"
#include "game.h"
#include "hooks.h"
#include "log.h"

namespace {

constexpr size_t kMaxCallbacks = 16;

struct Registered {
    const char* name;
    game_tick::Callback callback;
    bool disabled;
};

std::array<Registered, kMaxCallbacks> g_callbacks;
size_t g_callbackCount = 0;

using MoveFunction = void(__fastcall*)(void* self, void* edx);
MoveFunction g_originalMove = nullptr;
game_tick::MoveScope g_moveScope = nullptr;
game_tick::PostMove g_postMove = nullptr;
bool g_postMoveDisabled = false;

// Returns false when the callback raised an exception.
bool runGuarded(game_tick::Callback callback) {
    __try {
        callback();
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void runPostMove(uintptr_t player) {
    if (!g_postMove || g_postMoveDisabled) return;
    __try {
        g_postMove(player);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_postMoveDisabled = true;
        debug_stats::noteCallbackDisabled("post_move");
        debug_stats::setError("post-move callback raised an exception");
        logger::write("game_tick: post-move callback raised an exception, disabled");
    }
}

void tick() {
    for (size_t i = 0; i < g_callbackCount; ++i) {
        Registered& entry = g_callbacks[i];
        if (entry.disabled || runGuarded(entry.callback)) continue;
        entry.disabled = true;
        debug_stats::noteCallbackDisabled(entry.name);
        debug_stats::setError("callback %s raised an exception", entry.name);
        logger::write("game_tick: callback '%s' raised an exception, disabled", entry.name);
    }
}

void __fastcall moveDetour(void* self, void* edx) {
    const uintptr_t player = reinterpret_cast<uintptr_t>(self);
    if (player == game::controlled()) tick();
    if (g_moveScope) g_moveScope(player, true);
    g_originalMove(self, edx);
    if (g_moveScope) g_moveScope(player, false);
    runPostMove(player);
}

}  // namespace

namespace game_tick {

bool addCallback(const char* name, Callback callback) {
    if (g_callbackCount == kMaxCallbacks) {
        logger::write("game_tick: callback '%s' rejected, all %zu slots are in use", name, kMaxCallbacks);
        return false;
    }
    g_callbacks[g_callbackCount++] = {name, callback, false};
    return true;
}

void setMoveScope(MoveScope scope) { g_moveScope = scope; }

void setPostMove(PostMove postMove) { g_postMove = postMove; }

bool install() {
    if (!hooks::install("uPlayerBase::move", game::kPlayerMove, reinterpret_cast<void*>(&moveDetour),
                        reinterpret_cast<void**>(&g_originalMove))) {
        return false;
    }
    logger::write("game_tick: %zu callbacks", g_callbackCount);
    return true;
}

void uninstall() { hooks::remove(game::kPlayerMove); }

}  // namespace game_tick
