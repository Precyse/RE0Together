// DEATH STRANDING 2: the game stops its world when a menu pushes a pausing module state (weapon wheel, rings, pause menu):
// the main loop 0x14070f190 asks GameModule::IsPaused 0x140709c60 and, when it answers yes, takes the branch that calls the
// frame tick in mode 0 (no entity update, no world clock). IsPaused answers yes for any state of type 5..19 in the module's
// state list (+0x340 count, +0x348 array, type at state +0x10), or when the system pause flag [0x146266968] is set and
// 0x1426e7190() agrees. While linked and the local player is in gameplay, only the system part is kept: the menus stay
// pushed and usable, the world runs on (docs/DS2_NOTES.md, "World clock under menus"). Outside gameplay (a load, the title
// screen) and in cutscenes the game keeps its pause.
#include "ds2/world_pause.h"

#include <atomic>

#include "decima/safe_read.h"
#include "ds2/engine.h"
#include "ds2/sim_tick.h"
#include "game.h"
#include "hooks.h"
#include "log.h"

namespace {

constexpr uintptr_t kIsPaused = 0x140709c60;           // (GameModule*) -> bool
constexpr uintptr_t kSystemPauseFlag = 0x146266968;    // pointer: a system pause is requested while it is set
constexpr uintptr_t kSystemPauseAgrees = 0x1426e7190;  // () -> bool
constexpr uintptr_t kStateCount = 0x340, kStateArray = 0x348, kStateType = 0x10;
constexpr uint32_t kFirstMenuState = 5, kLastMenuState = 19;  // the pausing module states: wheel, rings, pause menu

using IsPausedFn = bool (*)(uintptr_t module);

IsPausedFn g_isPaused = nullptr;
std::atomic<bool> g_linked{false};
std::atomic<uint64_t> g_overridden{0};

bool menuStateUp(uintptr_t module) {
    const int32_t count = ds2::field<int32_t>(module, kStateCount);
    const uintptr_t array = decima::readPointer(module + kStateArray);
    for (int32_t i = 0; array && i < count; ++i) {
        const uintptr_t state = decima::readPointer(array + i * sizeof(uintptr_t));
        const uint32_t type = state ? ds2::field<uint32_t>(state, kStateType) : 0;
        if (type >= kFirstMenuState && type <= kLastMenuState) return true;
    }
    return false;
}

bool isPausedDetour(uintptr_t module) {
    const bool paused = g_isPaused(module);
    if (!paused || !g_linked.load() || !menuStateUp(module)) return paused;
    // A menu pushed while a load runs (or before gameplay has started) must pause the world: the engine updates half-built
    // or half-freed objects otherwise.
    if (!sim_tick::inWorld()) return paused;
    // Paused only because of a menu state: the system part decides.
    const bool system = decima::readPointer(ds2::at(kSystemPauseFlag)) != 0 &&
                        reinterpret_cast<bool (*)()>(ds2::at(kSystemPauseAgrees))();
    if (const uint64_t count = ++g_overridden; count == 1 || count % 5000 == 0) {
        logger::write("world_pause: kept the world running under a menu (%llu frames, system pause %d)",
                      static_cast<unsigned long long>(count), system);
    }
    return system;
}

}  // namespace

namespace world_pause {

void installEarly() {
    hooks::install("game module IsPaused", ds2::at(kIsPaused), reinterpret_cast<void*>(&isPausedDetour),
                   reinterpret_cast<void**>(&g_isPaused));
}

}  // namespace world_pause

namespace game {

void keepWorldRunning(bool linked) { g_linked = linked; }

}  // namespace game
