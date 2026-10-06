// DEATH STRANDING 2: guest restriction on orders. Terminals open for everyone; accepting an order goes through
// script-exported callbacks of the terminal's mission menu (DSUIMissionMenu). While the local player is a guest those
// callbacks return without calling the engine, with a toast, so nothing changes in either world; the host accepts.
// A guest turns in at its own terminal (the hand-over menu runs normally): the order's success request is the one the
// story sync forwards to the host (docs/DS2_NOTES.md, "Guest turn-in").
#include <atomic>
#include <utility>

#include "ds2/engine.h"
#include "game.h"
#include "hooks.h"
#include "log.h"
#include "toast_queue.h"

namespace {

constexpr float kToastSeconds = 4.0f;

struct Callback {
    const char* name;
    uintptr_t address;
    const char* refusal;
};

constexpr Callback kCallbacks[] = {
    {"mission list decide", 0x141751e40, "Only the host can accept orders"},
    {"mission default dialog yes", 0x141751ce0, "Only the host can accept orders"},
};
constexpr size_t kCallbackCount = sizeof(kCallbacks) / sizeof(kCallbacks[0]);

// The exported callbacks take script arguments (a menu and values); they are forwarded untouched.
using CallbackFn = void (*)(uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4, uintptr_t a5, uintptr_t a6);

std::atomic<bool> g_blocking{false};
CallbackFn g_originals[kCallbackCount] = {};

template <size_t Index>
void callbackDetour(uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4, uintptr_t a5, uintptr_t a6) {
    logger::write("order_gate: %s called%s", kCallbacks[Index].name, g_blocking.load() ? ", refused (guest)" : "");
    if (g_blocking.load()) {
        toast_queue::push(kCallbacks[Index].refusal, kToastSeconds);
        return;
    }
    g_originals[Index](a1, a2, a3, a4, a5, a6);
}

template <size_t... Indices>
void installAll(std::index_sequence<Indices...>) {
    (hooks::install(kCallbacks[Indices].name, ds2::at(kCallbacks[Indices].address),
                    reinterpret_cast<void*>(&callbackDetour<Indices>), reinterpret_cast<void**>(&g_originals[Indices])),
     ...);
}

}  // namespace

namespace game {

void watchOrders() { installAll(std::make_index_sequence<kCallbackCount>{}); }

void blockOrders(bool block) {
    if (g_blocking.exchange(block) == block) return;
    logger::write("order_gate: accepting orders %s", block ? "refused (guest)" : "allowed");
}

}  // namespace game
