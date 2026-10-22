#include "enemy_target.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

#include "character_owner.h"
#include "enemy_protocol.h"
#include "enemy_registry.h"
#include "enemy_state.h"
#include "game.h"
#include "hooks.h"
#include "log.h"
#include "net_pad.h"
#include "scene.h"
#include "settled_copy.h"
#include "split_rooms.h"
#include "target_hysteresis_rule.h"

namespace {

namespace rule = target_hysteresis_rule;
using character_owner::Character;

static_assert(rule::kNone == enemy_protocol::kNoTarget);
static_assert(static_cast<uint8_t>(Character::Unknown) == enemy_protocol::kNoTarget);

using SelectFunction = void(__fastcall*)(void* enemy, void* edx);

constexpr size_t kRowCount = game::kTargetSelectors.size();
constexpr size_t kVtableSlotBytes = sizeof(uint32_t);

std::array<SelectFunction, kRowCount> g_originals{};
SelectFunction g_originalOwnClassSelect = nullptr;

constexpr size_t kAxisX = 0;
constexpr size_t kAxisY = 1;
constexpr size_t kAxisZ = 2;

// What the owner remembers about one pool slot's enemy; it starts over when the slot holds another enemy, class or room.
struct Slot {
    uintptr_t enemy = 0;
    uintptr_t vtable = 0;
    uint16_t room = 0;
    rule::Memory memory;
};
std::array<Slot, game::kEnemyPoolSlots> g_slots{};  // game thread only

int64_t nowMs() { return static_cast<int64_t>(monotonicMs()); }

bool readPosition(uintptr_t unit, float (&out)[3]) { return game::readMemory(unit + game::kUnitPositionOffset, out); }

bool classMatches(const game::TargetSelector& row, uintptr_t vtable) {
    if (!vtable) return false;
    if (std::find(row.vtables.begin(), row.vtables.end(), vtable) != row.vtables.end()) return true;
    return row.matchUpdate &&
           game::readPointer(vtable + game::kEnemyUpdateSlot * kVtableSlotBytes) == row.matchUpdate;
}

const game::TargetSelector* rowOf(uintptr_t enemy) {
    const uintptr_t vtable = game::readPointer(enemy);
    for (const game::TargetSelector& row : game::kTargetSelectors) {
        if (classMatches(row, vtable)) return &row;
    }
    return nullptr;
}

const rule::Tuning* tuningOf(const game::TargetSelector& row) {
    switch (row.policy) {
        case game::TargetPolicy::LargeBoss: return &rule::kLargeBossTuning;
        case game::TargetPolicy::Stalker: return &rule::kStalkerTuning;
        case game::TargetPolicy::SyncOnly: break;
    }
    return nullptr;
}

Slot* slotFor(uintptr_t enemy) {
    const int index = enemy_registry::slotOf(enemy);
    if (index == enemy_registry::kNoSlot) return nullptr;
    Slot& slot = g_slots[static_cast<size_t>(index)];
    const uintptr_t vtable = game::readPointer(enemy);
    const uint16_t room = scene::current();
    if (slot.enemy != enemy || slot.vtable != vtable || slot.room != room) slot = Slot{enemy, vtable, room, {}};
    return &slot;
}

bool targetsFixedCharacter(uintptr_t enemy, const game::TargetSelector& row) {
    uint32_t word = 0;
    return row.preferenceOffset && game::readMemory(enemy + row.preferenceOffset, word) &&
           (word & row.preferenceMask) != 0;
}

// The two characters as the engine sees them: sPlayer's controlled one first, then the partner.
struct Candidates {
    uint8_t id[rule::kCharacterCount];
    float distance[rule::kCharacterCount];
};

Candidates candidatesOf(uintptr_t enemy) {
    Candidates candidates{};
    const uintptr_t objects[rule::kCharacterCount] = {game::controlled(), game::partner()};
    const uintptr_t distanceOffsets[rule::kCharacterCount] = {game::kEnemyDistanceControlledOffset,
                                                              game::kEnemyDistancePartnerOffset};
    for (size_t i = 0; i < rule::kCharacterCount; ++i) {
        candidates.id[i] = static_cast<uint8_t>(character_owner::identify(objects[i]));
        if (!game::readMemory(enemy + distanceOffsets[i], candidates.distance[i])) candidates.distance[i] = rule::kIneligible;
    }
    return candidates;
}

// Sets or clears the class's "target is Rebecca-type" flag.
void setRebeccaFlag(uintptr_t enemy, const game::TargetSelector& row, bool rebecca) {
    uint32_t word = 0;
    if (!row.rebeccaFlagOffset || !game::readMemory(enemy + row.rebeccaFlagOffset, word)) return;
    word = rebecca ? (word | row.rebeccaFlagMask) : (word & ~row.rebeccaFlagMask);
    game::writeMemory(enemy + row.rebeccaFlagOffset, word);
}

// Points a class's target fields at `player`, the way its selector fills them: the object, the distance the engine
// already computed for that character, the height and the Rebecca flag where the class has them.
void aim(uintptr_t enemy, const game::TargetSelector& row, uintptr_t player) {
    if (!player) return;
    float distance = 0.0f;
    float height = 0.0f;
    if (row.distanceOffset) {
        const uintptr_t source = player == game::controlled() ? game::kEnemyDistanceControlledOffset
                                                              : game::kEnemyDistancePartnerOffset;
        if (!game::readMemory(enemy + source, distance)) return;
    }
    if (row.heightOffset) {
        float enemyPosition[3];
        float playerPosition[3];
        if (!readPosition(enemy, enemyPosition) || !readPosition(player, playerPosition)) return;
        height = playerPosition[kAxisY] - enemyPosition[kAxisY];
    }
    game::writeMemory(enemy + row.targetOffset, static_cast<uint32_t>(player));
    if (row.distanceOffset) game::writeMemory(enemy + row.distanceOffset, distance);
    if (row.heightOffset) game::writeMemory(enemy + row.heightOffset, height);
    setRebeccaFlag(enemy, row, character_owner::identify(player) == Character::Rebecca);
}

// The own-class selector: the object and the flat (x, z) distance, as 0x521bd0 computes it.
void aimOwnClass(uintptr_t enemy, uintptr_t target) {
    float enemyPosition[3];
    float targetPosition[3];
    if (!readPosition(enemy, enemyPosition) || !readPosition(target, targetPosition)) return;
    const float flat = std::hypot(enemyPosition[kAxisX] - targetPosition[kAxisX],
                                  enemyPosition[kAxisZ] - targetPosition[kAxisZ]);
    game::writeMemory(enemy + game::kEnemyTargetOffset, static_cast<uint32_t>(target));
    game::writeMemory(enemy + game::kEnemyTargetFlatDistanceOffset, flat);
}

// The machine following the owner: the owner's character replaces this machine's choice.
void followOwner(uintptr_t enemy, const game::TargetSelector& row) {
    const uint8_t id = enemy_state::ownerTarget(enemy);
    if (id == enemy_protocol::kNoTarget) return;
    aim(enemy, row, character_owner::find(static_cast<Character>(id)));
}

void logSwitch(const Slot& slot, uint8_t from) {
    logger::write("enemy_target: slot %d switch %s -> %s (%s)", static_cast<int>(&slot - g_slots.data()),
                  character_owner::name(static_cast<Character>(from)),
                  character_owner::name(static_cast<Character>(slot.memory.current)),
                  rule::reasonName(slot.memory.reason));
}

// The room's enemy owner: keeps the class's target steady, and overrides the selector's result when it differs.
void holdTarget(uintptr_t enemy, const game::TargetSelector& row) {
    const rule::Tuning* tuning = tuningOf(row);
    Slot* slot = tuning ? slotFor(enemy) : nullptr;
    if (!slot) return;
    const Candidates candidates = candidatesOf(enemy);
    const uint8_t native = static_cast<uint8_t>(character_owner::identify(game::readPointer(enemy + row.targetOffset)));
    const uint8_t before = slot->memory.current;
    const uint8_t chosen = rule::choose(*tuning, slot->memory, native, candidates.distance, candidates.id, nowMs());
    if (chosen != before) logSwitch(*slot, before);
    if (chosen != rule::kNone && chosen != native) aim(enemy, row, character_owner::find(static_cast<Character>(chosen)));
}

void afterSelect(uintptr_t enemy, const game::TargetSelector& row) {
    if (!net_pad::active() || targetsFixedCharacter(enemy, row)) return;
    if (split_rooms::localEnemyAuthority()) {
        holdTarget(enemy, row);
    } else {
        followOwner(enemy, row);
    }
}

template <size_t Row>
void __fastcall selectDetour(void* enemy, void* edx) {
    g_originals[Row](enemy, edx);
    afterSelect(reinterpret_cast<uintptr_t>(enemy), game::kTargetSelectors[Row]);
}

template <size_t Row>
bool installRow() {
    return hooks::install("enemy target select", game::kTargetSelectors[Row].function,
                          reinterpret_cast<void*>(selectDetour<Row>), reinterpret_cast<void**>(&g_originals[Row]));
}

template <size_t... Rows>
bool installRows(std::index_sequence<Rows...>) {
    const std::array<bool, sizeof...(Rows)> installed = {installRow<Rows>()...};
    return std::all_of(installed.begin(), installed.end(), [](bool ok) { return ok; });
}

void __fastcall ownClassSelectDetour(void* enemy, void* edx) {
    g_originalOwnClassSelect(enemy, edx);
    const uintptr_t self = reinterpret_cast<uintptr_t>(enemy);
    const uint8_t id = enemy_state::ownerTarget(self);
    if (id == enemy_protocol::kNoTarget) return;
    const uintptr_t target = character_owner::find(static_cast<Character>(id));
    if (target) aimOwnClass(self, target);
}

}  // namespace

namespace enemy_target {

bool install() {
    const bool rows = installRows(std::make_index_sequence<kRowCount>());
    const bool ownClass = hooks::install("enemy own-class target select", game::kEnemyOwnClassTargetSelectFunction,
                                         reinterpret_cast<void*>(ownClassSelectDetour),
                                         reinterpret_cast<void**>(&g_originalOwnClassSelect));
    return rows && ownClass;
}

uint8_t read(uintptr_t enemy) {
    const game::TargetSelector* row = rowOf(enemy);
    uintptr_t offset = 0;
    if (row) {
        offset = row->targetOffset;
    } else if (game::readPointer(enemy) == game::kEnemyOwnClassVtable) {
        offset = game::kEnemyTargetOffset;
    }
    if (!offset) return enemy_protocol::kNoTarget;
    return static_cast<uint8_t>(character_owner::identify(game::readPointer(enemy + offset)));
}

void noteAttacker(uintptr_t enemy, uint8_t characterId) {
    const game::TargetSelector* row = rowOf(enemy);
    const rule::Tuning* tuning = row ? tuningOf(*row) : nullptr;
    Slot* slot = tuning ? slotFor(enemy) : nullptr;
    if (slot) rule::noteAttacker(*tuning, slot->memory, characterId, nowMs());
}

}  // namespace enemy_target
