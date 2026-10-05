#include "ds2/sim_tick.h"

#include <windows.h>

#include <atomic>
#include <cstdio>
#include <vector>

#include "ds2/engine.h"
#include "ds2/player.h"
#include "ds2/player_state.h"
#include "hooks.h"
#include "log.h"

namespace {

constexpr uintptr_t kObjectListUpdate = 0x140215460;  // the engine's per-frame update of live objects
constexpr double kReportMinMicros = 20.0;  // callbacks cheaper than this are not listed
constexpr double kReportSeconds = 5.0;  // how often the frame rate and the callbacks' cost are logged

// The update takes more than its first four arguments; they are passed through untouched.
using UpdateFn = uint64_t (*)(uintptr_t, float, float, uint8_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t);

UpdateFn g_update = nullptr;
struct Entry {
    sim_tick::Callback callback;
    const char* name;
    sim_tick::Gate gate;
    int64_t spent;  // simulation thread only: counter ticks this callback took in the report window
};

std::vector<Entry> g_entries;  // written at start-up only
std::atomic<uint32_t> g_epoch{0};
ds2::GameplayClock g_gameplay;  // simulation thread: the local player's state machine running

int64_t g_spent = 0;  // simulation thread only: counter ticks the callbacks took in this window

int64_t counter() {
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return now.QuadPart;
}

// Logs the frames per second of the update and what the registered callbacks cost per frame, every few seconds.
void report(int64_t now) {
    static int64_t frequency = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return f.QuadPart;
    }();
    static int64_t windowStart = now;
    static int frames = 0;
    ++frames;
    const double seconds = static_cast<double>(now - windowStart) / frequency;
    if (seconds < kReportSeconds) return;
    logger::write("sim_tick: %.1f frames/s, callbacks %.0f us/frame", frames / seconds,
                  frames ? 1e6 * static_cast<double>(g_spent) / frequency / frames : 0.0);
    for (Entry& entry : g_entries) {
        const double micros = frames ? 1e6 * static_cast<double>(entry.spent) / frequency / frames : 0.0;
        if (micros >= kReportMinMicros) {
            logger::write("sim_tick:   %s %.0f us/frame", entry.name, micros);
        }
        entry.spent = 0;
    }
    windowStart = now;
    g_spent = 0;
    frames = 0;
}

uint64_t updateDetour(uintptr_t self, float a, float b, uint8_t flag, uintptr_t s5, uintptr_t s6, uintptr_t s7,
                      uintptr_t s8) {
    const int64_t start = counter();
    const bool wasActive = g_gameplay.active();
    g_gameplay.update(ds2::localPlayerEntity());
    if (!wasActive && g_gameplay.active()) ++g_epoch;
    for (Entry& entry : g_entries) {
        if (entry.gate == sim_tick::Gate::Gameplay && !g_gameplay.active()) continue;
        const int64_t before = counter();
        entry.callback();
        entry.spent += counter() - before;
    }
    g_spent += counter() - start;
    report(start);
    return g_update(self, a, b, flag, s5, s6, s7, s8);
}

}  // namespace

namespace sim_tick {

void installEarly() {
    if (g_update) return;
    hooks::install("object list update", ds2::at(kObjectListUpdate), reinterpret_cast<void*>(&updateDetour),
                   reinterpret_cast<void**>(&g_update));
}

uint32_t gameplayEpoch() { return g_epoch.load(); }

void add(Callback callback, const char* name, Gate gate) { g_entries.push_back({callback, name, gate, 0}); }

}  // namespace sim_tick
