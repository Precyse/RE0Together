// DEATH STRANDING 2: guest restriction. Before the player claims a use location (the "F" interactions: terminals,
// order and quest triggers), the game asks the player entity MsgIsUseLocationClaimAllowed through the entity message
// dispatcher (0x1401618c0); any handler can refuse by setting the message's veto byte. While blocking, the adapter
// refuses after the game's own handlers ran, so the guest gets no prompt and never starts the interaction (or a
// terminal's autosave). The query does not say which use location is meant, so every claim is refused for now
// (docs/DS2_NOTES.md, "Guest restrictions").
#include <windows.h>

#include <atomic>

#include "decima/safe_read.h"
#include "ds2/player.h"
#include "game.h"
#include "hooks.h"
#include "log.h"
#include "msvc_rtti.h"
#include "pattern_scan.h"

namespace {

// Entity message dispatch: (handler list at entity +0x2D0, entity lock at +0x2A8, message, flags).
constexpr const char* kDispatch =
    "48 89 54 24 10 57 48 83 EC 40 48 89 5C 24 50 48 8B FA 48 89 74 24 68 48 8B F1 4C 89 6C 24 30 45 8B E9 "
    "4C 89 7C 24 20 4D";
constexpr const char* kClaimQuery = "MsgIsUseLocationClaimAllowed";
constexpr uintptr_t kEntityLock = 0x2A8;
constexpr uintptr_t kVeto = 0x10;  // MsgIsAllowedBase: byte, 0 when built (RTTI constructor 0x140831830), 1 = refused
constexpr uint8_t kRefused = 1;

using DispatchFn = void (*)(uintptr_t handlers, uintptr_t lock, uintptr_t message, uint32_t flags);

DispatchFn g_dispatch = nullptr;
uintptr_t g_claimQueryVtable = 0;
std::atomic<bool> g_blocking{false};

void dispatchDetour(uintptr_t handlers, uintptr_t lock, uintptr_t message, uint32_t flags) {
    g_dispatch(handlers, lock, message, flags);
    if (!g_blocking.load() || decima::readPointer(message) != g_claimQueryVtable) return;
    if (lock - kEntityLock != ds2::localPlayerEntity()) return;
    *reinterpret_cast<uint8_t*>(message + kVeto) = kRefused;
}

bool install() {
    g_claimQueryVtable = msvc_rtti::vtableOf(kClaimQuery);
    const uintptr_t dispatch = pattern_scan::find(kDispatch);
    if (!g_claimQueryVtable || !dispatch) {
        logger::write("use_gate: dispatcher %p or claim query %p not found", reinterpret_cast<void*>(dispatch),
                      reinterpret_cast<void*>(g_claimQueryVtable));
        return false;
    }
    return hooks::install("entity message dispatch", dispatch, reinterpret_cast<void*>(&dispatchDetour),
                          reinterpret_cast<void**>(&g_dispatch));
}

}  // namespace

namespace game {

void blockScriptedInteractions(bool block) {
    if (g_blocking.load() == block) return;
    static const bool installed = install();  // the first time a guest needs it; a host never hooks
    if (!installed) return;
    g_blocking = block;
    logger::write("use_gate: use-location claims %s", block ? "refused (guest)" : "allowed");
}

}  // namespace game
