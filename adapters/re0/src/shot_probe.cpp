#include "shot_probe.h"

#include <windows.h>

#include <cmath>
#include <cstdint>

#include "crash_dump.h"
#include "enemy_drift_rule.h"
#include "enemy_policy.h"
#include "enemy_registry.h"
#include "game.h"
#include "hooks.h"
#include "log.h"
#include "pose_wire.h"

namespace {

// sWeapon::hitscanWide(this, player, weaponType, flag) -> hit, thiscall ret 0xc (the shotgun's scan, 0x504b27).
using HitscanWide = uint8_t(__fastcall*)(void* weapon, void* edx, void* player, uint32_t weaponType, uint32_t flag);
// The scan's ray test of one hit shape: (shape, ray, radius data, out hit) -> hit, stdcall ret 0x10.
using RayTest = uint8_t(__stdcall*)(uintptr_t shape, uintptr_t ray, const void* radius, void* out);
// uPlayerBase::calcHitscanRay(this, out ray), thiscall ret 4: the scan's ray, built before any shape is tested.
using CalcRay = void(__fastcall*)(void* player, void* edx, uintptr_t out);

constexpr uintptr_t kHitscanWideFunction = 0x52fd10;
constexpr uintptr_t kRayTestFunction = 0x5301a0;
constexpr uintptr_t kCalcRayFunction = 0x50e9f0;
constexpr uintptr_t kShapeCenterFunction = 0x5267a0;  // thiscall(shape, out vec4) ret 4: the shape's world centre
constexpr uintptr_t kShapesOffset = 0x10a0;           // 20 hit shapes of 0x50 bytes; word 0 is the owner unit when active
constexpr size_t kShapeCount = 20;
constexpr uintptr_t kShapeSize = 0x50;
constexpr uintptr_t kShapeRadiusOffset = 0x30;
constexpr uintptr_t kShapeStartOffset = 0x10;  // a capsule runs from here to kShapeEndOffset (the shape's own space)
constexpr uintptr_t kShapeEndOffset = 0x20;
constexpr uintptr_t kRayDirectionOffset = 0x20;
constexpr uintptr_t kRayOriginOffset = 0x30;
constexpr uintptr_t kUnitMatrixTranslationOffset = 0xe0;  // row 3 of the matrix at +0xb0 the shapes are placed with
constexpr float kNearRay = 400.0f;  // an enemy whose nearest shape passes further from the ray was not aimed at

HitscanWide g_originalScan = nullptr;
RayTest g_originalRay = nullptr;
CalcRay g_originalCalcRay = nullptr;
enemy_drift_rule::LineBudget g_budget;

// Per scan, game thread.
bool g_scanning = false;
bool g_haveRay = false;
float g_origin[3] = {};
float g_direction[3] = {};
uint32_t g_enemyTests = 0;
uint32_t g_enemyHits = 0;

struct Vec {
    float x, y, z;
};

Vec readVec(uintptr_t address) {
    Vec v{};
    game::readMemory(address, v);
    return v;
}

float length(Vec v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }

Vec minus(Vec a, Vec b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }

// Distance from `point` to the ray (to its origin when the point lies behind it).
float rayGap(Vec point) {
    const Vec dir{g_direction[0], g_direction[1], g_direction[2]};
    const float dirLength = length(dir);
    const Vec rel = minus(point, {g_origin[0], g_origin[1], g_origin[2]});
    if (dirLength == 0.0f) return length(rel);
    const float along = (rel.x * dir.x + rel.y * dir.y + rel.z * dir.z) / dirLength;
    if (along <= 0.0f) return length(rel);
    const Vec foot{g_origin[0] + dir.x / dirLength * along, g_origin[1] + dir.y / dirLength * along, g_origin[2] + dir.z / dirLength * along};
    return length(minus(point, foot));
}

Vec shapeCenter(uintptr_t shape) {
    alignas(16) float out[4] = {};
    game::callThiscall<void>(kShapeCenterFunction, reinterpret_cast<void*>(shape), out);
    return {out[0], out[1], out[2]};
}

struct Nearest {
    int slot = -1;
    uintptr_t enemy = 0;
    float gap = 0.0f;          // nearest shape surface to the ray
    float shapeFromBody = 0.0f;  // that shape's centre to the enemy's position
    float matrixFromBody = 0.0f; // the shapes' matrix translation to the enemy's position
    float capsule = 0.0f;        // that shape's capsule length
    uint32_t shapes = 0;
};

// The living pool enemy whose hit shapes pass nearest the ray.
Nearest nearestEnemy() {
    Nearest best;
    for (int slot = 0; slot < game::kEnemyPoolSlots; ++slot) {
        const uintptr_t enemy = enemy_registry::enemyAt(slot);
        int32_t hp = 0;
        if (!enemy || !game::readMemory(enemy + game::kEnemyHpOffset, hp) || hp <= 0) continue;
        const Vec body = readVec(enemy + game::kPlayerPositionOffset);
        uint32_t shapes = 0;
        for (size_t i = 0; i < kShapeCount; ++i) {
            const uintptr_t shape = enemy + kShapesOffset + i * kShapeSize;
            if (game::readPointer(shape) != enemy) continue;
            ++shapes;
            float radius = 0.0f;
            game::readMemory(shape + kShapeRadiusOffset, radius);
            const Vec center = shapeCenter(shape);
            const float gap = rayGap(center) - radius;
            if (best.enemy && gap >= best.gap) continue;
            best = {slot, enemy, gap, length(minus(center, body)), length(minus(readVec(enemy + kUnitMatrixTranslationOffset), body)),
                    length(minus(readVec(shape + kShapeEndOffset), readVec(shape + kShapeStartOffset))), 0};
        }
        if (best.enemy == enemy) best.shapes = shapes;
    }
    return best;
}

void report() {
    const Nearest nearest = nearestEnemy();
    if (!nearest.enemy || nearest.gap > kNearRay || !g_budget.allow(pose_wire::clockMs())) return;
    logger::write("shot_probe: wide scan missed; slot %d (%s) nearest shape base %.0f from the ray, %u shapes, capsule %.0f long, base %.0f and matrix %.0f from its body; enemy shapes tested %u hit %u",
                  nearest.slot, enemy_policy::nameOf(enemy_policy::classIndexOf(game::readPointer(nearest.enemy))), nearest.gap,
                  nearest.shapes, nearest.capsule, nearest.shapeFromBody, nearest.matrixFromBody, g_enemyTests, g_enemyHits);
}

void __fastcall calcRayDetour(void* player, void* edx, uintptr_t out) {
    g_originalCalcRay(player, edx, out);
    if (g_scanning && !g_haveRay) {
        g_haveRay = game::readMemory(out + kRayOriginOffset, g_origin) && game::readMemory(out + kRayDirectionOffset, g_direction);
    }
}

uint8_t __stdcall rayDetour(uintptr_t shape, uintptr_t ray, const void* radius, void* out) {
    const uint8_t hit = g_originalRay(shape, ray, radius, out);
    if (g_scanning && enemy_registry::isEnemy(game::readPointer(shape))) {
        ++g_enemyTests;
        g_enemyHits += hit ? 1 : 0;
    }
    return hit;
}

uint8_t __fastcall scanDetour(void* weapon, void* edx, void* player, uint32_t weaponType, uint32_t flag) {
    g_scanning = true;
    g_haveRay = false;
    g_enemyTests = 0;
    g_enemyHits = 0;
    const uint8_t hit = g_originalScan(weapon, edx, player, weaponType, flag);
    g_scanning = false;
    if (g_enemyHits == 0 && g_haveRay) {
        __try {
            report();
        } __except (crash_dump::reportCaught(GetExceptionInformation(), "shot_probe: report")) {
        }
    }
    return hit;
}

}  // namespace

namespace shot_probe {

bool enable() {
    return hooks::install("sWeapon::hitscanWide", kHitscanWideFunction, reinterpret_cast<void*>(&scanDetour),
                          reinterpret_cast<void**>(&g_originalScan)) &&
           hooks::install("uPlayerBase::calcHitscanRay", kCalcRayFunction, reinterpret_cast<void*>(&calcRayDetour),
                          reinterpret_cast<void**>(&g_originalCalcRay)) &&
           hooks::install("hit shape ray test", kRayTestFunction, reinterpret_cast<void*>(&rayDetour),
                          reinterpret_cast<void**>(&g_originalRay));
}

}  // namespace shot_probe
