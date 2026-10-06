// DEATH STRANDING 2: log-only instruments for the partner-cargo checks (see ds2/orders_diag.h).
#include "ds2/orders_diag.h"

#include <intrin.h>

#include <atomic>
#include <mutex>
#include <set>

#include "decima/safe_read.h"
#include "ds2/engine.h"
#include "game.h"
#include "hooks.h"
#include "log.h"
#include "remote_body.h"

namespace {

constexpr uintptr_t kHeadlineGetter = 0x1414a13a0;  // (UI manager) -> DSUIBaggageCarrierSlotTypeResource, which names a carrier group
constexpr uintptr_t kOwnerActiveCheck = 0x14119b2e0;  // (owner) -> whether the menus may use the owner
constexpr uintptr_t kCarrierPanel = 0x141569dc0;  // (menu controller): shows the selected carrier; it asks the headline getter
constexpr uintptr_t kControllerModel = 0xE0, kModelSelected = 0xC1C, kModelOwnerCount = 0x48, kModelOwners = 0x50;
constexpr uintptr_t kOwnerCarrierType = 0x10, kOwnerKey = 0x18;
constexpr uintptr_t kMissionComplete = 0x141396730;  // (processor, mission, flag): an order in progress succeeds
constexpr uintptr_t kTrackerSetState = 0x1413e2890;  // (tracker, mission id, bag id, state, ...): a piece record's state
constexpr uintptr_t kMissionStateField = 0x22, kMissionIdField = 0x28;
constexpr size_t kMaxCallersLogged = 24;

std::atomic<bool> g_enabled{false};
std::mutex g_mutex;
std::set<uintptr_t> g_headlineCallers, g_carriedCallers, g_activeCallers, g_panelOwners;

uintptr_t fileVa(const void* address) {
    return reinterpret_cast<uintptr_t>(address) - ds2::at(ds2::kImageBase) + ds2::kImageBase;
}

// Logs `address` once per distinct value (up to a limit).
bool firstTime(std::set<uintptr_t>& seen, uintptr_t address) {
    std::lock_guard lock(g_mutex);
    return seen.size() < kMaxCallersLogged && seen.insert(address).second;
}

using GetterFn = uintptr_t (*)(uintptr_t ui);
using ActiveFn = uint64_t (*)(uintptr_t owner);
using Fn8 = uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t);
Fn8 g_originalComplete = nullptr, g_originalTrackerState = nullptr;
using PanelFn = void (*)(uintptr_t controller, uintptr_t a2, uintptr_t a3, uintptr_t a4, uintptr_t a5, uintptr_t a6);
PanelFn g_originalPanel = nullptr;
GetterFn g_originalGetter = nullptr;
ActiveFn g_originalActive = nullptr;

uintptr_t getterDetour(uintptr_t ui) {
    const uintptr_t caller = fileVa(_ReturnAddress());
    if (firstTime(g_headlineCallers, caller)) logger::write("orders_diag: the carrier headline getter was called from %p", reinterpret_cast<void*>(caller));
    return g_originalGetter(ui);
}

// Which menu code asks whether the remote's owner is usable, and what the game answers (the owner is switched off on purpose).
uint64_t activeDetour(uintptr_t owner) {
    const uint64_t answer = g_originalActive(owner);
    const auto key = remote_body::ownerKey();
    if (key && owner == game::baggageOwner(*key)) {
        const uintptr_t caller = fileVa(_ReturnAddress());
        if (firstTime(g_activeCallers, caller)) {
            logger::write("orders_diag: the owner-active check on the remote's owner was called from %p and answered %llu",
                          reinterpret_cast<void*>(caller), static_cast<unsigned long long>(answer & 0xFF));
        }
    }
    return answer;
}

// The delivery's own effects, in order: every piece record state change and the order's completion.
uintptr_t completeDetour(uintptr_t a1, uintptr_t mission, uintptr_t a3, uintptr_t a4, uintptr_t a5, uintptr_t a6, uintptr_t a7,
                         uintptr_t a8) {
    uint16_t state = 0;
    uint64_t id = 0;
    decima::safeRead(mission + kMissionStateField, state);
    decima::safeRead(mission + kMissionIdField, id);
    logger::write("orders_diag: order %llx completes (state %u) from %p", static_cast<unsigned long long>(id), state,
                  reinterpret_cast<void*>(fileVa(_ReturnAddress())));
    return g_originalComplete(a1, mission, a3, a4, a5, a6, a7, a8);
}

uintptr_t trackerStateDetour(uintptr_t a1, uintptr_t missionId, uintptr_t bagId, uintptr_t state, uintptr_t a5, uintptr_t a6,
                             uintptr_t a7, uintptr_t a8) {
    logger::write("orders_diag: piece record of order %llx bag %llx set to state %llu from %p",
                  static_cast<unsigned long long>(missionId), static_cast<unsigned long long>(bagId),
                  static_cast<unsigned long long>(state & 0xFF), reinterpret_cast<void*>(fileVa(_ReturnAddress())));
    return g_originalTrackerState(a1, missionId, bagId, state, a5, a6, a7, a8);
}

// The owner the carrier panel is about to show (the controller's selected entry), logged once per distinct owner.
void panelDetour(uintptr_t controller, uintptr_t a2, uintptr_t a3, uintptr_t a4, uintptr_t a5, uintptr_t a6) {
    const uintptr_t model = decima::readPointer(controller + kControllerModel);
    int32_t selected = -1, count = 0;
    decima::safeRead(model + kModelSelected, selected);
    decima::safeRead(model + kModelOwnerCount, count);
    const uintptr_t owners = decima::readPointer(model + kModelOwners);
    const uintptr_t owner = selected >= 0 && selected < count && owners ? decima::readPointer(owners + selected * sizeof(uintptr_t)) : 0;
    uint8_t type = 0;
    uint64_t key = 0;
    if (owner && decima::safeRead(owner + kOwnerCarrierType, type) && decima::safeRead(owner + kOwnerKey, key) &&
        firstTime(g_panelOwners, owner)) {
        const auto remoteKey = remote_body::ownerKey();
        logger::write("orders_diag: the carrier panel shows owner %p (type %u, key %llx, the remote's: %s)",
                      reinterpret_cast<void*>(owner), type, static_cast<unsigned long long>(key),
                      remoteKey && *remoteKey == key ? "yes" : "no");
    }
    g_originalPanel(controller, a2, a3, a4, a5, a6);
}

}  // namespace

namespace orders_diag {

void installEarly() {
    g_enabled = true;
    hooks::install("orders headline getter", ds2::at(kHeadlineGetter), reinterpret_cast<void*>(&getterDetour),
                   reinterpret_cast<void**>(&g_originalGetter));
    hooks::install("orders carrier panel", ds2::at(kCarrierPanel), reinterpret_cast<void*>(&panelDetour),
                   reinterpret_cast<void**>(&g_originalPanel));
    hooks::install("orders completion", ds2::at(kMissionComplete), reinterpret_cast<void*>(&completeDetour),
                   reinterpret_cast<void**>(&g_originalComplete));
    hooks::install("orders piece record state", ds2::at(kTrackerSetState), reinterpret_cast<void*>(&trackerStateDetour),
                   reinterpret_cast<void**>(&g_originalTrackerState));
    hooks::install("orders owner-active check", ds2::at(kOwnerActiveCheck), reinterpret_cast<void*>(&activeDetour),
                   reinterpret_cast<void**>(&g_originalActive));
}

void noteHandOverGather(uintptr_t query, uintptr_t remoteOwner, bool appended) {
    if (!g_enabled.load()) return;
    logger::write("orders_diag: the hand-over gather %p finished, the remote's owner %s", reinterpret_cast<void*>(query),
                  !remoteOwner ? "does not exist" : appended ? "was missing and carries order pieces: appended" : "was left as it is");
}

void noteTerminalAdd(uint32_t type, uint64_t orderId, uintptr_t terminal) {
    if (!g_enabled.load()) return;
    logger::write("orders_diag: piece kind %u (order %llx) added to the terminal owner %p", type,
                  static_cast<unsigned long long>(orderId), reinterpret_cast<void*>(terminal));
}

void noteCarriedSet(const void* caller, uint32_t added) {
    if (!g_enabled.load()) return;
    const uintptr_t at = fileVa(caller);
    if (firstTime(g_carriedCallers, at)) {
        const auto key = remote_body::ownerKey();
        const size_t owned = key ? game::ownedCargo(*key).size() : 0;
        logger::write("orders_diag: the carried set (scoped to the remote's owner) was called from %p, the walk added %u pieces, the owner holds %zu",
                      reinterpret_cast<void*>(at), added, owned);
    }
}

}  // namespace orders_diag
