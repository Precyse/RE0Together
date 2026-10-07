#include "enemy_target.h"

#include <cmath>

#include "character_owner.h"
#include "enemy_state.h"
#include "game.h"
#include "hooks.h"

namespace {

using SelectFunction = void(__fastcall*)(void* enemy, void* edx);

SelectFunction g_originalBaseSelect = nullptr;
SelectFunction g_originalOwnClassSelect = nullptr;

constexpr size_t kAxisX = 0;
constexpr size_t kAxisY = 1;
constexpr size_t kAxisZ = 2;

bool readPosition(uintptr_t unit, float (&out)[3]) { return game::readMemory(unit + game::kUnitPositionOffset, out); }

// Points the base classes' target fields (object, height, distance) at `target`, the way their selector fills them.
void aimBase(uintptr_t enemy, uintptr_t target) {
    const uintptr_t distanceOffset = target == game::controlled() ? game::kEnemyDistanceControlledOffset
                                                                  : game::kEnemyDistancePartnerOffset;
    float distance = 0.0f;
    float enemyPosition[3];
    float targetPosition[3];
    if (!game::readMemory(enemy + distanceOffset, distance) || !readPosition(enemy, enemyPosition) ||
        !readPosition(target, targetPosition)) {
        return;
    }
    game::writeMemory(enemy + game::kEnemyTargetOffset, static_cast<uint32_t>(target));
    game::writeMemory(enemy + game::kEnemyTargetHeightOffset, targetPosition[kAxisY] - enemyPosition[kAxisY]);
    game::writeMemory(enemy + game::kEnemyTargetDistanceOffset, distance);
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

// After a selector ran: on the machine following the owner, the owner's character replaces its choice.
void followOwner(void* enemy, void (*aim)(uintptr_t enemy, uintptr_t target)) {
    const uintptr_t self = reinterpret_cast<uintptr_t>(enemy);
    const uint8_t id = enemy_state::ownerTarget(self);
    if (id == enemy_protocol::kNoTarget) return;
    const uintptr_t target = character_owner::find(static_cast<character_owner::Character>(id));
    if (target) aim(self, target);
}

void __fastcall baseSelectDetour(void* enemy, void* edx) {
    g_originalBaseSelect(enemy, edx);
    followOwner(enemy, aimBase);
}

void __fastcall ownClassSelectDetour(void* enemy, void* edx) {
    g_originalOwnClassSelect(enemy, edx);
    followOwner(enemy, aimOwnClass);
}

}  // namespace

namespace enemy_target {

bool install() {
    const bool base = hooks::install("enemy target select", game::kEnemyBaseTargetSelectFunction,
                                     reinterpret_cast<void*>(baseSelectDetour),
                                     reinterpret_cast<void**>(&g_originalBaseSelect));
    const bool ownClass = hooks::install("enemy own-class target select", game::kEnemyOwnClassTargetSelectFunction,
                                         reinterpret_cast<void*>(ownClassSelectDetour),
                                         reinterpret_cast<void**>(&g_originalOwnClassSelect));
    return base && ownClass;
}

uint8_t read(uintptr_t enemy) {
    static_assert(static_cast<uint8_t>(character_owner::Character::Unknown) == enemy_protocol::kNoTarget);
    return static_cast<uint8_t>(character_owner::identify(game::readPointer(enemy + game::kEnemyTargetOffset)));
}

}  // namespace enemy_target
