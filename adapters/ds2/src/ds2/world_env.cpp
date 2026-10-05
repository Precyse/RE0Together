// DEATH STRANDING 2: the world's clock and weather (GameWorldTimeState and DSWeatherManager). The host reads both; a
// guest follows them: the game's own setters run on the game's threads from detours on the time and weather updates,
// and the guest's forecast is pinned to the host's so it never fires a forecast of its own.
#include "ds2/world_env.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <mutex>

#include "decima/safe_read.h"
#include "ds2/engine.h"
#include "ds2/sim_tick.h"
#include "game.h"
#include "hooks.h"
#include "log.h"

namespace {

constexpr uintptr_t kGameModuleGlobal = 0x14623e338;
constexpr uintptr_t kWeatherManagerGlobal = 0x14623fa10;
constexpr uintptr_t kTimeUpdate = 0x1406eaa90;           // (GameWorldTimeState*, u32 delta ms)
constexpr uintptr_t kAdvanceSeconds = 0x1406eae90;     // (GameWorldTimeState*, float seconds): adds seconds / 3600 hours, wraps the day
constexpr uintptr_t kWeatherUpdate = 0x141ef3240;        // (DSWeatherManager*, float dt)
constexpr uintptr_t kSetTimeOfDay = 0x14074f240;         // Game_SetTimeOfDay(float hours)
constexpr uintptr_t kSetRegionTypeDirect = 0x141f09a40;  // (u8 region, u8 state type)

constexpr uintptr_t kModuleTimeState = 0x58;
constexpr uintptr_t kTimeHours = 0x20, kTimeDay = 0x28, kTimePaused = 0x39;
constexpr uintptr_t kWeatherSlot = 0x68c0, kWeatherClock = 0x68c8, kWeatherThreshold = 0x68d0;
constexpr int kForecastSlots = 4;  // region tables of 64 entries: the table ends where the slot index starts
constexpr uintptr_t kRegionTable = 0x8c0;
constexpr uintptr_t kRegionEntrySize = 0x60;
constexpr ULONGLONG kPausedAfterMs = 150;   // no clock update this long: the game's frame tick is paused (menus, the weapon wheel)
constexpr ULONGLONG kMaxAdvanceMs = 100;    // a longer gap is one step, not a catch-up
constexpr ULONGLONG kMaxRateGapMs = 100;    // calls further apart than this do not measure the rate
constexpr double kSecondsPerHour = 3600.0;
constexpr float kSnapDriftHours = 0.02f;  // about 1.2 game minutes: closer than this the guest's own clock is kept

using AdvanceFn = void (*)(uintptr_t state, float seconds);
using TimeUpdateFn = void (*)(uintptr_t state, uint32_t deltaMs);
using WeatherUpdateFn = void (*)(uintptr_t manager, float dt);
using SetTimeFn = void (*)(float hours);
using SetRegionFn = void (*)(uint8_t region, uint8_t type);

TimeUpdateFn g_timeUpdate = nullptr;
std::atomic<bool> g_keepRunning{false};         // linked: the world goes on while a menu pauses this machine's frame tick
std::atomic<ULONGLONG> g_lastUpdateMs{0};
std::atomic<double> g_hoursPerMs{0.0};           // game hours per wall millisecond, measured while the clock runs
WeatherUpdateFn g_weatherUpdate = nullptr;

std::mutex g_mutex;  // guards the target
bool g_following = false;
bool g_timePending = false;
bool g_weatherPending = false;
env_wire::WorldEnv g_target{};

uintptr_t timeState() {
    const uintptr_t module = decima::readPointer(ds2::at(kGameModuleGlobal));
    return module ? decima::readPointer(module + kModuleTimeState) : 0;
}

uintptr_t weatherManager() { return decima::readPointer(ds2::at(kWeatherManagerGlobal)); }

uintptr_t regionEntry(uintptr_t manager, int slot, int region) {
    return manager + kRegionTable + (static_cast<uintptr_t>(slot) * env_wire::kRegionCount + region) * kRegionEntrySize;
}

// Measures how fast the game's clock runs (only updates that moved it count), per wall millisecond, from the hours before and after its own update.
void noteTimeUpdate(uintptr_t state, float hoursBefore) {
    const ULONGLONG now = GetTickCount64();
    float after = hoursBefore;
    decima::safeRead(state + kTimeHours, after);
    const float advanced = after - hoursBefore;
    if (advanced <= 0) return;  // the update ran and did nothing (the game's own state check): not a running clock
    const ULONGLONG gap = now - g_lastUpdateMs.exchange(now);
    if (gap > 0 && gap <= kMaxRateGapMs && advanced > 0) g_hoursPerMs = static_cast<double>(advanced) / static_cast<double>(gap);
}

// The game stops the clock with the rest of its frame tick under the weapon wheel, rings and the pause menu; while linked
// the world must go on (the partner's clock does), so the clock is stepped here from the simulation tick.
void keepClockRunning() {
    static ULONGLONG lastStep = 0;
    const ULONGLONG now = GetTickCount64();
    const double rate = g_hoursPerMs.load();
    const uintptr_t state = timeState();
    if (!g_keepRunning.load() || rate <= 0 || !state || now - g_lastUpdateMs.load() < kPausedAfterMs) {
        lastStep = 0;
        return;
    }
    if (lastStep) {
        const double hours = rate * static_cast<double>(std::min(now - lastStep, kMaxAdvanceMs));
        reinterpret_cast<AdvanceFn>(ds2::at(kAdvanceSeconds))(state, static_cast<float>(hours * kSecondsPerHour));
    }
    lastStep = now;
}

// On the game's thread, after the clock's own update: snaps the time to the host's when it drifted.
void timeDetour(uintptr_t state, uint32_t deltaMs) {
    const float hoursBefore = ds2::field<float>(state, kTimeHours);
    g_timeUpdate(state, deltaMs);
    noteTimeUpdate(state, hoursBefore);
    env_wire::WorldEnv target;
    {
        std::lock_guard lock(g_mutex);
        if (!g_following || !g_timePending) return;
        g_timePending = false;
        target = g_target;
    }
    if (env_wire::hoursApart(ds2::field<float>(state, kTimeHours), target.timeOfDay) > kSnapDriftHours) {
        reinterpret_cast<SetTimeFn>(ds2::at(kSetTimeOfDay))(target.timeOfDay);
    }
    ds2::field<int32_t>(state, kTimeDay) = target.day;
    ds2::field<uint8_t>(state, kTimePaused) = target.flags & env_wire::kFlagTimePaused;
}

// On the game's thread, after the weather's own update: the host's region types, then the host's forecast clock so a
// forecast cannot fire here.
void weatherDetour(uintptr_t manager, float dt) {
    g_weatherUpdate(manager, dt);
    env_wire::WorldEnv target;
    bool changeTypes;
    {
        std::lock_guard lock(g_mutex);
        if (!g_following) return;
        target = g_target;
        changeTypes = g_weatherPending;
        g_weatherPending = false;
    }
    const int slot = ds2::field<int32_t>(manager, kWeatherSlot);
    if (changeTypes) {
        for (int region = 0; region < env_wire::kRegionCount; ++region) {
            const uint8_t wanted = target.regionType[region];
            if (wanted == env_wire::kRegionNone || ds2::field<uint32_t>(regionEntry(manager, slot, region), 0) == wanted) {
                continue;
            }
            reinterpret_cast<SetRegionFn>(ds2::at(kSetRegionTypeDirect))(static_cast<uint8_t>(region), wanted);
        }
    }
    ds2::field<float>(manager, kWeatherClock) = target.forecastClock;
    ds2::field<float>(manager, kWeatherThreshold) = target.nextThreshold;
}

}  // namespace

namespace world_env {

void installEarly() {
    sim_tick::add(&keepClockRunning, "world clock", sim_tick::Gate::Gameplay);
    hooks::install("time of day update", ds2::at(kTimeUpdate), reinterpret_cast<void*>(&timeDetour),
                   reinterpret_cast<void**>(&g_timeUpdate));
    hooks::install("weather update", ds2::at(kWeatherUpdate), reinterpret_cast<void*>(&weatherDetour),
                   reinterpret_cast<void**>(&g_weatherUpdate));
}

}  // namespace world_env

namespace game {

bool readWorldEnv(env_wire::WorldEnv& out) {
    const uintptr_t time = timeState();
    const uintptr_t weather = weatherManager();
    int32_t slot = 0;
    if (!time || !weather || !decima::safeRead(weather + kWeatherSlot, slot) || slot < 0 || slot >= kForecastSlots) {
        return false;
    }
    uint8_t paused = 0;
    decima::safeRead(time + kTimePaused, paused);
    out = {};
    out.flags = paused ? env_wire::kFlagTimePaused : 0;
    out.slot = static_cast<uint8_t>(slot);
    decima::safeRead(time + kTimeHours, out.timeOfDay);
    decima::safeRead(time + kTimeDay, out.day);
    decima::safeRead(weather + kWeatherClock, out.forecastClock);
    decima::safeRead(weather + kWeatherThreshold, out.nextThreshold);
    for (int region = 0; region < env_wire::kRegionCount; ++region) {
        uint32_t type = env_wire::kRegionNone;
        decima::safeRead(regionEntry(weather, slot, region), type);
        out.regionType[region] = static_cast<uint8_t>(type);
    }
    return true;
}

void followWorldEnv(const env_wire::WorldEnv& env) {
    std::lock_guard lock(g_mutex);
    g_target = env;
    g_following = true;
    g_timePending = true;
    g_weatherPending = true;
}

void keepWorldClockRunning(bool linked) { g_keepRunning = linked; }

void releaseWorldEnv() {
    std::lock_guard lock(g_mutex);
    g_following = false;
}

}  // namespace game
