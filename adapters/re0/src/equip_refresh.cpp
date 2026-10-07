#include "equip_refresh.h"

#include <windows.h>

#include <array>
#include <optional>

#include "debug_stats.h"
#include "equip_rule.h"
#include "game.h"
#include "game_state.h"
#include "log.h"
#include "settled_copy.h"

namespace {

using character_owner::Character;
using equip_rule::kNoWeapon;

struct Job {
    bool pending = false;  // the equipped slot changed and the character is not re-equipped yet
    bool planned = false;  // the sets of the latest change are released and requested
    int32_t requested = kNoWeapon;
    uint64_t startMs = 0;
};

std::array<Job, character_owner::kCharacterCount> g_jobs;                       // game thread
std::array<std::optional<int32_t>, character_owner::kCharacterCount> g_seenType;  // game thread: last weapon type logged

template <class Step>
bool guarded(Step step) {
    __try {
        step();
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void* gameChara() { return reinterpret_cast<void*>(game::readPointer(game::kGameCharaGlobal)); }

// The character's item block (0x50dc70) and its equipped slot, or null.
void* itemBlock(uintptr_t player, uint32_t& slot) {
    void* block = game::callThiscall<void*>(game::kPlayerItemBlockFunction, reinterpret_cast<void*>(player));
    if (!block || !game::readMemory(reinterpret_cast<uintptr_t>(block) + game::kBlockEquippedOffset, slot)) return nullptr;
    return block;
}

// The weapon type the character holds and the one of its block's equipped slot (setEquipped, the call 0x5d7fa0 makes).
bool readTypes(uintptr_t player, int32_t& held, int32_t& next) {
    uint32_t slot = equip_rule::kNoSlot;
    void* block = itemBlock(player, slot);
    if (!block || !game::readMemory(player + game::kPlayerWeaponTypeOffset, held)) return false;
    next = game::callThiscall<int32_t>(game::kBlockSetEquippedFunction, block, slot);
    return true;
}

// The menu close's resource step for the latest change (release 0x520850, request 0x5203d0).
bool loadSets(uintptr_t player, Job& job) {
    void* chara = gameChara();
    int32_t held = kNoWeapon;
    int32_t next = kNoWeapon;
    uint32_t record = 0;
    if (!chara || !readTypes(player, held, next) ||
        !game::readMemory(player + game::kPlayerSceneRecordOffset, record)) {
        return false;
    }
    const equip_rule::LoadPlan plan = equip_rule::planLoad(held, next, job.requested);
    if (plan.releaseRequested) game::callThiscall<void>(game::kReleaseSetFunction, chara, game::kWeaponUnitKind, job.requested);
    if (plan.releaseHeld) game::callThiscall<void>(game::kReleaseSetFunction, chara, game::kWeaponUnitKind, held);
    if (plan.requestNext) game::callThiscall<void>(game::kRequestSetFunction, chara, game::kWeaponUnitKind, next, record);
    job.requested = plan.requested;
    return true;
}

bool requestsLoaded() {
    void* chara = gameChara();
    return chara && game::callThiscall<bool>(game::kRequestsLoadedFunction, chara);
}

// The menu close's equip step for one player (0x5d7785..0x5d77ab).
void equip(uintptr_t player) {
    auto* self = reinterpret_cast<void*>(player);
    uint32_t slot = equip_rule::kNoSlot;
    void* block = itemBlock(player, slot);
    if (!block) return;
    const int32_t type = game::callThiscall<int32_t>(game::kBlockSetEquippedFunction, block, slot);
    game::callThiscall<void>(game::kPlayerSetWeaponTypeFunction, self, type);
    if (slot == equip_rule::kNoSlot) return;
    game::callThiscall<void>(game::kPlayerWeaponAttachFunction, self);
    game::callThiscall<void>(game::kPlayerWeaponAimFunction, self);
}

int32_t heldType(uintptr_t player) {
    int32_t type = kNoWeapon;
    game::readMemory(player + game::kPlayerWeaponTypeOffset, type);
    return type;
}

const char* ownerLabel(Character character) { return character_owner::isLocalOwned(character) ? "local" : "remote"; }

void fail(Character character, Job& job, const char* step) {
    logger::write("equip: the %s faulted for %s", step, character_owner::name(character));
    job = Job{};
}

// Plain gameplay (no menu, no door) with the character in the loaded room; a change that arrives earlier waits.
bool canEquip(uintptr_t player) {
    return game_state::playing() && !game_state::menuOpen() && game_state::inCurrentRoom(player);
}

void advance(Character character, uintptr_t player, Job& job) {
    if (!job.planned) {
        bool requested = false;
        if (!guarded([&] { requested = loadSets(player, job); })) return fail(character, job, "set request");
        if (!requested) return;
        job.planned = true;
        job.startMs = monotonicMs();
    }
    bool ready = job.requested == kNoWeapon;
    if (!ready && !guarded([&] { ready = requestsLoaded(); })) return fail(character, job, "set load check");
    if (!ready) return;
    const int32_t before = heldType(player);
    if (!guarded([&] { equip(player); })) return fail(character, job, "equip step");
    const int32_t after = heldType(player);
    logger::write("equip: %s weapon %d -> %d (%s, synced) after %llu ms, weapon set %s", character_owner::name(character),
                  before, after, ownerLabel(character), monotonicMs() - job.startMs,
                  job.requested == kNoWeapon ? "not needed" : "loaded");
    debug_stats::count(debug_stats::Counter::EquipRefreshes);
    g_seenType[static_cast<size_t>(character)] = after;
    job = Job{};
}

// A weapon change made by the game itself (the local menu, a room load).
void logGameChange(Character character, uintptr_t player) {
    std::optional<int32_t>& seen = g_seenType[static_cast<size_t>(character)];
    const int32_t type = heldType(player);
    if (seen == type) return;
    if (seen) {
        bool ready = false;
        guarded([&] { ready = requestsLoaded(); });
        logger::write("equip: %s weapon %d -> %d (%s, game), weapon sets loaded=%d", character_owner::name(character),
                      *seen, type, ownerLabel(character), ready ? 1 : 0);
    }
    seen = type;
}

}  // namespace

namespace equip_refresh {

void request(Character character) {
    Job& job = g_jobs[static_cast<size_t>(character)];
    job.pending = true;
    job.planned = false;
}

void tick(Character character) {
    Job& job = g_jobs[static_cast<size_t>(character)];
    if (job.pending && !character_owner::isRemoteOwned(character)) job = Job{};
    const uintptr_t player = character_owner::find(character);
    if (!player) return;
    if (job.pending && canEquip(player)) advance(character, player, job);
    logGameChange(character, player);
}

}  // namespace equip_refresh
