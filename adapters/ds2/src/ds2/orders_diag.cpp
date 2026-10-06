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
constexpr size_t kMaxCallersLogged = 24;

std::atomic<bool> g_enabled{false};
std::mutex g_mutex;
std::set<uintptr_t> g_headlineCallers, g_carriedCallers, g_activeCallers;

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

}  // namespace

namespace orders_diag {

void installEarly() {
    g_enabled = true;
    hooks::install("orders headline getter", ds2::at(kHeadlineGetter), reinterpret_cast<void*>(&getterDetour),
                   reinterpret_cast<void**>(&g_originalGetter));
    hooks::install("orders owner-active check", ds2::at(kOwnerActiveCheck), reinterpret_cast<void*>(&activeDetour),
                   reinterpret_cast<void**>(&g_originalActive));
}

void noteHandOverGather(uintptr_t query, uintptr_t remoteOwner, bool appended) {
    if (!g_enabled.load()) return;
    logger::write("orders_diag: the hand-over gather %p finished, the remote's owner %s", reinterpret_cast<void*>(query),
                  !remoteOwner ? "does not exist" : appended ? "was missing and carries order pieces: appended" : "was left as it is");
}

void noteSlotAdd(bool toRemote, uintptr_t origin, bool originIsRemote, bool originIsLocal) {
    if (!g_enabled.load()) return;
    logger::write("orders_diag: a piece was added to the %s owner, origin %p (%s)", toRemote ? "remote's" : "local player's",
                  reinterpret_cast<void*>(origin), originIsRemote ? "the remote's" : originIsLocal ? "the local player's" : "other or unknown");
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
