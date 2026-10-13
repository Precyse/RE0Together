// DEATH STRANDING 2: log-only instruments for the BT catch on the host (see ds2/bt_diag.h).
#include "ds2/bt_diag.h"

#include <array>
#include <atomic>
#include <string>

#include "decima/safe_read.h"
#include "ds2/craters.h"
#include "ds2/engine.h"
#include "ds2/entity_lookup.h"
#include "ds2/remote_player.h"
#include "hooks.h"
#include "log.h"

namespace {

constexpr uintptr_t kGazerCatchEvent = 0x1419cacf0;   // DSGazerAnimationComponent MsgAnimationEventStarts handler
constexpr uintptr_t kHunterClingEvent = 0x141abf1d0;  // DSHunterClingComponent MsgAnimationEventStarts handler
constexpr uintptr_t kComponentOwner = 0x48;           // a component's entity
constexpr uintptr_t kAnnihilationTarget = 0x50;       // DSCatcherAnnihilationComponent: the entity the annihilation hits
constexpr int kIndexSam = 0;
constexpr int kIndexRemoteBody = 1;
constexpr int kIndexOther = -1;

// The handlers are message handlers called with the component and the message; every register argument is passed through.
using HandlerFn = uint64_t (*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t);

HandlerFn g_gazerCatch = nullptr;
HandlerFn g_hunterCling = nullptr;
std::atomic<bool> g_enabled{false};

std::string ownerText(uintptr_t component) {
    std::array<uint8_t, 16> uuid{};
    if (!ds2::entityUuid(decima::readPointer(component + kComponentOwner), uuid)) return "unreadable";
    return craters::uuidText(uuid.data());
}

// The player index of a pointer that is a player entity or a weak reference to one.
int playerIndex(uintptr_t value) {
    for (const uintptr_t candidate : {value, value - ds2::kEntityWeakTarget}) {
        if (candidate && candidate == remote_player::samEntity()) return kIndexSam;
        if (candidate && candidate == remote_player::entity()) return kIndexRemoteBody;
    }
    return kIndexOther;
}

uint64_t gazerDetour(uintptr_t component, uintptr_t a, uintptr_t b, uintptr_t c) {
    logger::write("bt_diag: gazer catch event, gazer %s", ownerText(component).c_str());
    return g_gazerCatch(component, a, b, c);
}

uint64_t hunterDetour(uintptr_t component, uintptr_t a, uintptr_t b, uintptr_t c) {
    logger::write("bt_diag: hunter cling event, hunter %s", ownerText(component).c_str());
    return g_hunterCling(component, a, b, c);
}

}  // namespace

namespace bt_diag {

void installEarly() {
    hooks::install("gazer catch event", ds2::at(kGazerCatchEvent), reinterpret_cast<void*>(&gazerDetour),
                   reinterpret_cast<void**>(&g_gazerCatch));
    hooks::install("hunter cling event", ds2::at(kHunterClingEvent), reinterpret_cast<void*>(&hunterDetour),
                   reinterpret_cast<void**>(&g_hunterCling));
    g_enabled = true;
}

void annihilation(uintptr_t component, bool begin) {
    if (!g_enabled.load()) return;
    const uintptr_t target = decima::readPointer(component + kAnnihilationTarget);
    logger::write("bt_diag: annihilation %s, catcher %s, target %p, player index %d", begin ? "starts" : "ends",
                  ownerText(component).c_str(), reinterpret_cast<void*>(target), playerIndex(target));
}

}  // namespace bt_diag
