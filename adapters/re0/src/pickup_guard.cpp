#include "pickup_guard.h"

#include "debug_stats.h"
#include "floor_items_sync.h"
#include "game.h"
#include "game_state.h"
#include "hooks.h"
#include "item_ledger.h"
#include "log.h"
#include "state_mode.h"

namespace {

using StepFunction = uint32_t(__fastcall*)(void* state, void* edx, void* player);

// State mode: how far from the taker the floor item its pickup is about to take can be, to find it among the floor items.
constexpr float kPickupReach = 300.0f;

StepFunction g_originalStep = nullptr;

// The take step reads [player + 0x67a0] + 4 without a null check (0x4ded40 then 0x500da3). The target is cleared
// when the other player's pickup removes the item first, so a missing target means the take must not run.
bool targetMissing(uintptr_t player) {
    return player && game::readPointer(player + game::kPlayerInteractTargetOffset) == 0;
}

// State mode: the item the player's pickup is about to take. The player points at the item's data record (id at +4,
// count at +8); the floor item that carries it is the nearest of that id.
bool readTake(uintptr_t player, item_ledger::Take& out) {
    const uintptr_t record = game::readPointer(player + game::kPlayerInteractTargetOffset);
    uint32_t itemId = 0;
    uint16_t count = 0;
    item_ledger::Vec3 playerPos{};
    item_ledger::Vec3 itemPos{};
    if (!record || !game::readMemory(record + game::kItemRecordIdOffset, itemId) ||
        !game::readMemory(record + game::kItemRecordCountOffset, count) ||
        !game::readMemory(player + game::kUnitPositionOffset, playerPos) ||
        !floor_items_sync::locateItem(itemId, playerPos, kPickupReach, itemPos)) {
        return false;
    }
    out = {game_state::currentRoom(), itemId, count, itemPos};
    return true;
}

// State mode: the pickup's take waits for the host ledger. False holds the engine's step this frame; a refusal also ends
// the pickup the way a vanished target does.
bool ledgerAllowsStep(uintptr_t state, uint32_t phase) {
    if (phase == game::kPickupStartPhase) {
        item_ledger::resetTake();
        return true;
    }
    if (phase != game::kPickupTakePhase) return true;
    item_ledger::Take take{};
    if (!readTake(game::readPointer(state + game::kPickupPlayerOffset), take)) {
        logger::writeUnlessRepeated("pickup_guard: take target not found, not arbitrated");
        return true;
    }
    const item_ledger::Resolution resolution = item_ledger::resolveTake(take);
    if (resolution.requested) logger::write("pickup_guard: take held for the host");
    if (resolution.verdict == item_ledger::Verdict::End &&
        game::writeMemory(state + game::kPickupPhaseOffset, game::kPickupDonePhase)) {
        debug_stats::count(debug_stats::Counter::PickupsAborted);
        logger::write("pickup_guard: take refused (%s)", item_ledger_rule::name(resolution.answer));
    }
    return resolution.verdict == item_ledger::Verdict::Run;
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
    if (state_mode::on() && !ledgerAllowsStep(address, phase)) return 0;
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
