// DEATH STRANDING 2: the guest-side vetoes of the BT grab (see ds2/bt_grab.h).
// Every BT that grabs a player sends MsgRequestPlayerInteractiveAction (vtable 0x143335d88: +0x10 a weak pointer to the
// instigator, +0x30 the action kind) to the player entity; the DSPlayerState handler table maps it to 0x140e1db40
// (DSPlayerState*, message*), which hands it to the state's interactive action plugin. The gazer's catch animation event
// 0x1419cacf0 takes the nearest catcher territory, activates it and sends kind 2 to the player.
#include "ds2/bt_grab.h"

#include <atomic>

#include "ds2/bt_diag.h"
#include "ds2/checked_hook.h"
#include "ds2/enemy_puppet.h"
#include "ds2/engine.h"
#include "ds2/player_state.h"
#include "ds2/remote_player.h"
#include "log.h"
#include "remote_apply.h"

namespace {

constexpr uintptr_t kGazerCatchEvent = 0x1419cacf0;      // DSGazerAnimationComponent MsgAnimationEventStarts handler (component, message, ...)
constexpr const char* kGazerCatchStart = "48 83 EC 28 48 8B 41 30 4C 8B 40 28 4C 39 42 10";
constexpr uintptr_t kInteractiveRequest = 0x140e1db40;   // (DSPlayerState*, MsgRequestPlayerInteractiveAction*)
constexpr const char* kInteractiveRequestStart = "48 8B 89 40 07 00 00 48 85 C9";
constexpr uintptr_t kMessageInstigator = 0x10;           // the weak pointer node to the instigator entity
constexpr uintptr_t kMessageAction = 0x30;               // u8 action kind

using HandlerFn = uint64_t (*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t);
using RequestFn = uint64_t (*)(uintptr_t, uintptr_t);

HandlerFn g_gazerCatch = nullptr;
RequestFn g_interactive = nullptr;
std::atomic<bool> g_guest{false};
bool g_diagnostics = false;

uint64_t gazerDetour(uintptr_t component, uintptr_t message, uintptr_t c, uintptr_t d) {
    if (g_diagnostics) logger::write("bt_grab: gazer catch event, gazer %s", bt_diag::ownerText(component).c_str());
    if (g_guest.load() && !remote_apply::active()) {
        static bool logged = false;
        if (!logged) logger::write("bt_grab: a puppet gazer's own catch event is vetoed (the host's catch is replayed)");
        logged = true;
        return 0;
    }
    return g_gazerCatch(component, message, c, d);
}

// The local player's DSPlayerState asked to be grabbed by a puppet.
bool grabbedByPuppet(uintptr_t state, uintptr_t message) {
    return enemy_puppet::isPuppet(ds2::weakEntity(message + kMessageInstigator)) &&
           state == ds2::playerState(remote_player::samEntity());
}

uint64_t interactiveDetour(uintptr_t state, uintptr_t message) {
    if (g_guest.load() && !remote_apply::active() && grabbedByPuppet(state, message)) {
        uint8_t action = 0;
        decima::safeRead(message + kMessageAction, action);
        static bool logged = false;
        if (!logged) logger::write("bt_grab: a puppet's request on the local player is vetoed (action %u)", action);
        logged = true;
        return 0;
    }
    return g_interactive(state, message);
}

}  // namespace

namespace bt_grab {

void installEarly(bool diagnostics) {
    g_diagnostics = diagnostics;
    checked_hook::install("gazer catch event", kGazerCatchEvent, kGazerCatchStart, reinterpret_cast<void*>(&gazerDetour),
                          reinterpret_cast<void**>(&g_gazerCatch));
    checked_hook::install("interactive action request", kInteractiveRequest, kInteractiveRequestStart,
                          reinterpret_cast<void*>(&interactiveDetour), reinterpret_cast<void**>(&g_interactive));
}

void setRole(bool guest) { g_guest = guest; }

}  // namespace bt_grab
