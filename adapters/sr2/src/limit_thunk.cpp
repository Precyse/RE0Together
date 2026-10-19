#include "limit_thunk.h"

#include "code_patch.h"
#include "log.h"
#include "memory.h"
#include "sr2_addresses.h"
#include "vehicle_pool.h"

namespace {

uint32_t g_maxPlayers = sr2::kCoopDefaultPlayers;
const uintptr_t g_activateAllocatorSet = sr2::kActivateAllocatorSet;

void raiseLimits() {
    const uint32_t oldSession = dwordField(sr2::kMaxSessionPlayers, 0);
    const uint32_t oldPlayers = dwordField(sr2::kMaxPlayers, 0);
    dwordField(sr2::kMaxSessionPlayers, 0) = g_maxPlayers;
    dwordField(sr2::kMaxPlayers, 0) = g_maxPlayers;
    logger::write("coop limits: max session players %u -> %u, max players %u -> %u", oldSession, g_maxPlayers,
                  oldPlayers, g_maxPlayers);
    vehicle_pool::onCoopActivation(g_maxPlayers);
}

// Stands in for the activation call: every register and the flags survive (edi holds the set name) and the
// activation function returns straight to net_set_mode.
void __declspec(naked) activationThunk() {
    __asm {
        pushad
        pushfd
        call raiseLimits
        popfd
        popad
        jmp dword ptr [g_activateAllocatorSet]
    }
}

}  // namespace

namespace limit_thunk {

bool install(uint32_t maxPlayers) {
    g_maxPlayers = maxPlayers;
    const auto original = relativeBranch(kCallRel32, sr2::kActivateCoopSetsCall, sr2::kActivateAllocatorSet);
    const auto retargeted =
        relativeBranch(kCallRel32, sr2::kActivateCoopSetsCall, reinterpret_cast<uintptr_t>(&activationThunk));
    const bool ok = applyCodePatches({{"activate_coop_sets_call", sr2::kActivateCoopSetsCall,
                                       {original.begin(), original.end()}, {retargeted.begin(), retargeted.end()}}});
    if (ok) logger::write("coop limits: activation call at 0x%X retargeted", static_cast<unsigned>(sr2::kActivateCoopSetsCall));
    return ok;
}

}  // namespace limit_thunk
