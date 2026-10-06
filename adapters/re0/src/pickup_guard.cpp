#include "pickup_guard.h"

#include "character_owner.h"
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

// A remote-owned character's pickup belongs to its own machine: the prompt, the answer (take, use, decline) and the
// take happen there. Only the confirmed result reaches this one: FLOOR_TAKE when the item really leaves the floor
// (sItemPut::remove, whether taken or used) and INVENTORY / state sync for what it changed. The replayed press must
// not start the interaction here, or the prompt opens on both machines.
bool remoteOwned(uintptr_t player) {
    return player && character_owner::isRemoteOwned(character_owner::identify(player));
}

bool endAction(uintptr_t action, const char* reason) {
    if (!game::writeMemory(action + game::kPickupPhaseOffset, game::kPickupDonePhase)) return false;
    debug_stats::count(debug_stats::Counter::PickupsAborted);
    logger::writeUnlessRepeated(reason);
    return true;
}

uint32_t __fastcall stepDetour(void* state, void* edx, void* player) {
    const uintptr_t action = reinterpret_cast<uintptr_t>(state);
    uint32_t phase = 0;
    if (!game::readMemory(action + game::kPickupPhaseOffset, phase) || phase >= game::kPickupDonePhase) {
        return g_originalStep(state, edx, player);
    }
    const uintptr_t picker = game::readPointer(action + game::kPickupPlayerOffset);
    if (remoteOwned(picker) && endAction(action, "pickup_guard: the peer's pickup is not replayed here")) return 0;
    if (phase == game::kPickupTakePhase && targetMissing(picker) &&
        endAction(action, "pickup_guard: pickup target is gone, pickup aborted")) {
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
