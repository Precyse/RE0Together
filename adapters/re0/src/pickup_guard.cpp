#include "pickup_guard.h"

#include "debug_stats.h"
#include "game.h"
#include "hooks.h"
#include "log.h"

namespace {

using StepFunction = uint32_t(__fastcall*)(void* state, void* edx, void* player);

StepFunction g_originalStep = nullptr;

// The take step reads [player + 0x67a0] + 4 without a null check (0x4ded40 then 0x500da3). The target is cleared
// when the other player's pickup removes the item first, so a missing target means the take must not run.
bool targetMissing(uintptr_t player) {
    return player && game::readPointer(player + game::kPlayerInteractTargetOffset) == 0;
}

uint32_t __fastcall stepDetour(void* state, void* edx, void* player) {
    const uintptr_t address = reinterpret_cast<uintptr_t>(state);
    uint32_t phase = 0;
    if (game::readMemory(address + game::kPickupPhaseOffset, phase) && phase == game::kPickupTakePhase &&
        targetMissing(game::readPointer(address + game::kPickupPlayerOffset)) &&
        game::writeMemory(address + game::kPickupPhaseOffset, game::kPickupDonePhase)) {
        debug_stats::count(debug_stats::Counter::PickupsAborted);
        logger::writeUnlessRepeated("pickup_guard: pickup target is gone, pickup aborted");
        return 0;
    }
    return g_originalStep(state, edx, player);
}

}  // namespace

namespace pickup_guard {

bool enable() {
    return hooks::install("pickup step", game::kPickupStepFunction, reinterpret_cast<void*>(&stepDetour),
                          reinterpret_cast<void**>(&g_originalStep));
}

void uninstall() { hooks::remove(game::kPickupStepFunction); }

}  // namespace pickup_guard
