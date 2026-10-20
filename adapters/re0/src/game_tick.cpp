#include "game_tick.h"

#include <windows.h>

#include <array>
#include <atomic>
#include <cstdio>
#include <vector>

#include "crash_dump.h"
#include "debug_overlay.h"
#include "debug_stats.h"
#include "game.h"
#include "hooks.h"
#include "log.h"
#include "slow_frame.h"

namespace {

constexpr float kStoppedToastSeconds = 5.0f;
constexpr size_t kTextLength = 96;

struct Registered {
    const char* name;
    game_tick::Callback callback;
    bool disabled;
    slow_frame::Category category;
};

struct RegisteredPostMove {
    const char* name;
    game_tick::PostMove postMove;
    bool disabled;
    slow_frame::Category category;
};

struct RegisteredMoveScope {
    game_tick::MoveScope scope;
    const char* name;
    slow_frame::Category category;
    bool disabled;  // game thread only
};

std::vector<Registered> g_callbacks;  // filled before install(), then only read on the game thread
std::vector<RegisteredPostMove> g_postMoves;  // likewise
std::atomic<bool> g_rearmRequested{false};

using MoveFunction = void(__fastcall*)(void* self, void* edx);
MoveFunction g_originalMove = nullptr;
std::array<RegisteredMoveScope, game_tick::kMaxMoveScopes> g_moveScopes{};
std::atomic<size_t> g_moveScopeCount{0};  // scopes published to the game thread

LONG report(EXCEPTION_POINTERS* info, const char* name) {
    char what[kTextLength];
    snprintf(what, sizeof(what), "game_tick: '%s'", name);
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

bool runMoveScopeGuarded(const RegisteredMoveScope& entry, uintptr_t player, bool entering) {
    __try {
        entry.scope(player, entering);
        return true;
    } __except (report(GetExceptionInformation(), entry.name)) {
        return false;
    }
}

bool runPostMoveGuarded(const RegisteredPostMove& entry, uintptr_t player) {
    __try {
        entry.postMove(player);
        return true;
    } __except (report(GetExceptionInformation(), entry.name)) {
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

void rearmMoveScopes() {
    const size_t scopes = g_moveScopeCount.load(std::memory_order_acquire);
    for (size_t i = 0; i < scopes; ++i) rearm(g_moveScopes[i].name, g_moveScopes[i].disabled);
}

void runPostMoves(uintptr_t player) {
    for (RegisteredPostMove& entry : g_postMoves) {
        const slow_frame::Scope timing(entry.category);
        if (!entry.disabled && !runPostMoveGuarded(entry, player)) stop(entry.name, entry.disabled);
    }
}

void tick() {
    slow_frame::beginFrame();
    if (g_rearmRequested.exchange(false)) {
        for (Registered& entry : g_callbacks) rearm(entry.name, entry.disabled);
        for (RegisteredPostMove& entry : g_postMoves) rearm(entry.name, entry.disabled);
        rearmMoveScopes();
    }
    for (Registered& entry : g_callbacks) {
        const slow_frame::Scope timing(entry.category);
        if (!entry.disabled && !runGuarded(entry.callback, entry.name)) stop(entry.name, entry.disabled);
    }
}

// A scope that raised an exception stays off (enter and exit) until the next resync, like a callback.
void runMoveScope(RegisteredMoveScope& entry, uintptr_t player, bool entering) {
    if (entry.disabled) return;
    const slow_frame::Scope timing(entry.category);
    if (!runMoveScopeGuarded(entry, player, entering)) stop(entry.name, entry.disabled);
}

void __fastcall moveDetour(void* self, void* edx) {
    const uintptr_t player = reinterpret_cast<uintptr_t>(self);
    if (player == game::controlled()) tick();
    const size_t scopes = g_moveScopeCount.load(std::memory_order_acquire);
    for (size_t i = 0; i < scopes; ++i) runMoveScope(g_moveScopes[i], player, true);
    g_originalMove(self, edx);
    for (size_t i = scopes; i-- > 0;) runMoveScope(g_moveScopes[i], player, false);
    runPostMoves(player);
}

}  // namespace

namespace game_tick {

void addCallback(const char* name, Callback callback) {
    g_callbacks.push_back({name, callback, false, slow_frame::categoryOf(name)});
}

void addMoveScope(MoveScope scope, const char* name) {
    const size_t count = g_moveScopeCount.load();
    if (count >= kMaxMoveScopes) return;
    g_moveScopes[count] = {scope, name, slow_frame::categoryOf(name), false};
    g_moveScopeCount.store(count + 1, std::memory_order_release);
}

void addPostMove(const char* name, PostMove postMove) {
    g_postMoves.push_back({name, postMove, false, slow_frame::categoryOf(name)});
}

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
