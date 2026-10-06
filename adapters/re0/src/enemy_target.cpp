#include "enemy_target.h"

#include "character_owner.h"
#include "enemy_state.h"
#include "game.h"
#include "hooks.h"

namespace {

using SelectFunction = void(__fastcall*)(void* enemy, void* edx);

SelectFunction g_originalSelect = nullptr;

bool readHeight(uintptr_t enemy, uintptr_t target, float& out) {
    float enemyY = 0.0f;
    float targetY = 0.0f;
    if (!game::readMemory(enemy + game::kUnitPositionOffset + sizeof(float), enemyY) ||
        !game::readMemory(target + game::kUnitPositionOffset + sizeof(float), targetY)) {
        return false;
    }
    out = targetY - enemyY;
    return true;
}

// Points the enemy's target fields (object, height, distance) at `target`, the way the selector fills them.
void aim(uintptr_t enemy, uintptr_t target) {
    const uintptr_t distanceOffset = target == game::controlled() ? game::kEnemyDistanceControlledOffset
                                                                  : game::kEnemyDistancePartnerOffset;
    float distance = 0.0f;
    float height = 0.0f;
    if (!game::readMemory(enemy + distanceOffset, distance) || !readHeight(enemy, target, height)) return;
    game::writeMemory(enemy + game::kEnemyTargetOffset, static_cast<uint32_t>(target));
    game::writeMemory(enemy + game::kEnemyTargetHeightOffset, height);
    game::writeMemory(enemy + game::kEnemyTargetDistanceOffset, distance);
}

void __fastcall selectDetour(void* enemy, void* edx) {
    g_originalSelect(enemy, edx);
    const uintptr_t self = reinterpret_cast<uintptr_t>(enemy);
    const uint8_t id = enemy_state::puppetTarget(self);
    if (id == enemy_protocol::kNoTarget) return;
    const uintptr_t target = character_owner::find(static_cast<character_owner::Character>(id));
    if (target) aim(self, target);
}

}  // namespace

namespace enemy_target {

bool install() {
    return hooks::install("enemy target select", game::kEnemyTargetSelectFunction,
                          reinterpret_cast<void*>(selectDetour), reinterpret_cast<void**>(&g_originalSelect));
}

uint8_t read(uintptr_t enemy) {
    static_assert(static_cast<uint8_t>(character_owner::Character::Unknown) == enemy_protocol::kNoTarget);
    return static_cast<uint8_t>(character_owner::identify(game::readPointer(enemy + game::kEnemyTargetOffset)));
}

}  // namespace enemy_target
