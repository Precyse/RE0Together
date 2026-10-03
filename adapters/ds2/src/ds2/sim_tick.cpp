#include "ds2/sim_tick.h"

#include <vector>

#include "ds2/engine.h"
#include "hooks.h"

namespace {

constexpr uintptr_t kObjectListUpdate = 0x140215460;  // the engine's per-frame update of live objects
constexpr size_t kMaxCallbacks = 8;

// The update takes more than its first four arguments; they are passed through untouched.
using UpdateFn = uint64_t (*)(uintptr_t, float, float, uint8_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t);

UpdateFn g_update = nullptr;
sim_tick::Callback g_callbacks[kMaxCallbacks] = {};
size_t g_count = 0;  // written at start-up only

uint64_t updateDetour(uintptr_t self, float a, float b, uint8_t flag, uintptr_t s5, uintptr_t s6, uintptr_t s7,
                      uintptr_t s8) {
    for (size_t i = 0; i < g_count; ++i) g_callbacks[i]();
    return g_update(self, a, b, flag, s5, s6, s7, s8);
}

}  // namespace

namespace sim_tick {

void installEarly() {
    if (g_update) return;
    hooks::install("object list update", ds2::at(kObjectListUpdate), reinterpret_cast<void*>(&updateDetour),
                   reinterpret_cast<void**>(&g_update));
}

void add(Callback callback) {
    if (g_count < kMaxCallbacks) g_callbacks[g_count++] = callback;
}

}  // namespace sim_tick
