#include "game_tick.h"

#include <windows.h>

#include <atomic>
#include <cstdio>
#include <vector>

#include "crash_dump.h"
#include "debug_overlay.h"
#include "debug_stats.h"
#include "game.h"
#include "hooks.h"
#include "log.h"

namespace {

constexpr float kStoppedToastSeconds = 5.0f;
constexpr size_t kTextLength = 96;
constexpr char kPostMoveName[] = "post_move";

struct Registered {
    const char* name;
    game_tick::Callback callback;
    bool disabled;
};

std::vector<Registered> g_callbacks;  // filled before install(), then only read on the game thread
std::atomic<bool> g_rearmRequested{false};

using MoveFunction = void(__fastcall*)(void* self, void* edx);
MoveFunction g_originalMove = nullptr;
game_tick::MoveScope g_moveScope = nullptr;
game_tick::PostMove g_postMove = nullptr;
bool g_postMoveDisabled = false;

LONG report(EXCEPTION_POINTERS* info, const char* name) {
    char what[kTextLength];
    snprintf(what, sizeof(what), "game_tick: callback '%s'", name);
    return crash_dump::reportCaught(info, what);
}

// Returns false when the callback raised an exception (reported with its code and address).
bool runGuarded(game_tick::Callback callback, const char* name) {
    __try {
        callback();
        return true;
    } __except (report(GetExceptionInformation(), name)) {
        return false;
    }
}

bool runPostMoveGuarded(uintptr_t player) {
    __try {
        g_postMove(player);
        return true;
    } __except (report(GetExceptionInformation(), kPostMoveName)) {
        return false;
    }
}

// A faulted callback stays off until the next resync: running it again every frame would fault every frame.
void stop(const char* name, bool& disabled) {
    disabled = true;
    debug_stats::noteCallbackDisabled(name);
    debug_stats::setError("callback %s raised an exception", name);
    logger::write("game_tick: '%s' disabled until the next resync", name);
    char text[kTextLength];
    snprintf(text, sizeof(text), "Sync stopped: %s", name);
    debug_overlay::toast(text, kStoppedToastSeconds);
}

void rearm(const char* name, bool& disabled) {
    if (!disabled) return;
    disabled = false;
    debug_stats::noteCallbackRearmed(name);
    logger::write("game_tick: '%s' re-armed by a resync", name);
}

void runPostMove(uintptr_t player) {
    if (g_postMove && !g_postMoveDisabled && !runPostMoveGuarded(player)) stop(kPostMoveName, g_postMoveDisabled);
}

void tick() {
    if (g_rearmRequested.exchange(false)) {
        for (Registered& entry : g_callbacks) rearm(entry.name, entry.disabled);
        rearm(kPostMoveName, g_postMoveDisabled);
    }
    for (Registered& entry : g_callbacks) {
        if (!entry.disabled && !runGuarded(entry.callback, entry.name)) stop(entry.name, entry.disabled);
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

void addCallback(const char* name, Callback callback) { g_callbacks.push_back({name, callback, false}); }

void setMoveScope(MoveScope scope) { g_moveScope = scope; }

void setPostMove(PostMove postMove) { g_postMove = postMove; }

void rearmAfterResync() { g_rearmRequested = true; }

bool install() {
    if (!hooks::install("uPlayerBase::move", game::kPlayerMove, reinterpret_cast<void*>(&moveDetour),
                        reinterpret_cast<void**>(&g_originalMove))) {
        return false;
    }
    logger::write("game_tick: %zu callbacks", g_callbacks.size());
    return true;
}

void uninstall() { hooks::remove(game::kPlayerMove); }

}  // namespace game_tick
