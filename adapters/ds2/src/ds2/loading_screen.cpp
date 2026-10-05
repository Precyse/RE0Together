#include "ds2/loading_screen.h"

#include <atomic>

#include "ds2/engine.h"
#include "hooks.h"
#include "log.h"

namespace {

constexpr uintptr_t kConstructor = 0x1414c0bd0;       // (DSUILoadingScreenMenuFunction*)
constexpr uintptr_t kScalarDestructor = 0x1414c0c70;  // (object, flags)

using Clock = std::chrono::steady_clock;
using ConstructorFn = uintptr_t (*)(uintptr_t object);
using DestructorFn = uintptr_t (*)(uintptr_t object, uintptr_t flags);

ConstructorFn g_construct = nullptr;
DestructorFn g_destruct = nullptr;
std::atomic<int> g_alive{0};
std::atomic<Clock::rep> g_goneSince{0};  // steady-clock ticks of the last time the screen went away, 0 while never shown

uintptr_t constructDetour(uintptr_t object) {
    const uintptr_t result = g_construct(object);
    if (g_alive.fetch_add(1) == 0) logger::write("loading_screen: shown");
    return result;
}

uintptr_t destructDetour(uintptr_t object, uintptr_t flags) {
    const uintptr_t result = g_destruct(object, flags);
    if (g_alive.fetch_sub(1) == 1) {
        g_goneSince = Clock::now().time_since_epoch().count();
        logger::write("loading_screen: gone");
    }
    return result;
}

}  // namespace

namespace loading_screen {

void installEarly() {
    hooks::install("loading screen constructor", ds2::at(kConstructor), reinterpret_cast<void*>(&constructDetour),
                   reinterpret_cast<void**>(&g_construct));
    hooks::install("loading screen destructor", ds2::at(kScalarDestructor), reinterpret_cast<void*>(&destructDetour),
                   reinterpret_cast<void**>(&g_destruct));
}

bool shown() { return g_alive.load() > 0; }

bool goneFor(std::chrono::milliseconds period) {
    if (shown()) return false;
    const Clock::rep since = g_goneSince.load();
    return !since || Clock::now() - Clock::time_point(Clock::duration(since)) >= period;
}

}  // namespace loading_screen
