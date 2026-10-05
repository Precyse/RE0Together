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
constexpr uintptr_t kHandOverGather = 0x14119b930;  // (query): fills the owner list at +0x12810 for the hand-over menu
constexpr uintptr_t kGatherScanFrom = 0x12008, kGatherScanTo = 0x12818;  // the query's list fields (+0x12008 list, +0x12808 count, +0x12810 gathered)
constexpr size_t kMaxCallersLogged = 24;
constexpr uintptr_t kImageBase = 0x140000000;

std::atomic<bool> g_enabled{false};
std::mutex g_mutex;
std::set<uintptr_t> g_headlineCallers, g_carriedCallers;

uintptr_t fileVa(const void* address) { return reinterpret_cast<uintptr_t>(address) - ds2::at(kImageBase) + kImageBase; }

// Logs `address` once per distinct value (up to a limit).
bool firstTime(std::set<uintptr_t>& seen, uintptr_t address) {
    std::lock_guard lock(g_mutex);
    return seen.size() < kMaxCallersLogged && seen.insert(address).second;
}

using GetterFn = uintptr_t (*)(uintptr_t ui);
using GatherFn = uintptr_t (*)(uintptr_t query);
GetterFn g_originalGetter = nullptr;
GatherFn g_originalGather = nullptr;

uintptr_t getterDetour(uintptr_t ui) {
    const uintptr_t caller = fileVa(_ReturnAddress());
    if (firstTime(g_headlineCallers, caller)) logger::write("orders_diag: the carrier headline getter was called from %p", reinterpret_cast<void*>(caller));
    return g_originalGetter(ui);
}

uintptr_t gatherDetour(uintptr_t query) {
    const uintptr_t result = g_originalGather(query);
    const auto key = remote_body::ownerKey();
    const uintptr_t owner = key ? game::baggageOwner(*key) : 0;
    bool listed = false;
    for (uintptr_t at = query + kGatherScanFrom; owner && at < query + kGatherScanTo; at += sizeof(uintptr_t)) {
        if (decima::readPointer(at) == owner) listed = true;
    }
    logger::write("orders_diag: the hand-over gather finished, the remote's owner %s", !owner ? "does not exist" : listed ? "is listed" : "is NOT listed");
    return result;
}

}  // namespace

namespace orders_diag {

void installEarly() {
    g_enabled = true;
    hooks::install("orders headline getter", ds2::at(kHeadlineGetter), reinterpret_cast<void*>(&getterDetour),
                   reinterpret_cast<void**>(&g_originalGetter));
    hooks::install("orders hand-over gather", ds2::at(kHandOverGather), reinterpret_cast<void*>(&gatherDetour),
                   reinterpret_cast<void**>(&g_originalGather));
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
