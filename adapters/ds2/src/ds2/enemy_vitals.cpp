// DEATH STRANDING 2: an enemy's health. Any entity answers GetHealth / GetMaxHealth through its virtual slots 10 and 11
// (0x140134710 / 0x140134770); NPC enemies keep it in the life block of their damage component (component +0x168: valid
// bit at +0x28, current at +0x34, maximum at +0x38; tools/ds2/out/analysis/ENEMIES.md).
#include "ds2/enemy_vitals.h"

#include <algorithm>

#include "decima/entity.h"
#include "decima/safe_read.h"
#include "ds2/engine.h"
#include "ds2/entity_lookup.h"
#include "enemy_wire.h"
#include "msvc_rtti.h"

namespace {

constexpr size_t kGetHealthSlot = 10, kGetMaxHealthSlot = 11;
constexpr uintptr_t kLifeBlock = 0x168, kLifeValid = 0x28, kLifeCurrent = 0x34, kLifeMax = 0x38;
constexpr uint8_t kHealthFull = 254;
constexpr float kMinimumLeft = 0.02f;  // of the maximum
constexpr const char* kDamageComponents[] = {"DSBanditDamageComponent", "DSSneakingNpcDamageComponent",
                                             "DSGhostMechDamageComponent", "DSBTDamageComponent"};

using HealthFn = float (*)(uintptr_t entity);

float slotValue(uintptr_t entity, size_t slot) {
    const uintptr_t vtable = decima::readPointer(entity);
    const uintptr_t fn = vtable ? decima::readPointer(vtable + slot * sizeof(uintptr_t)) : 0;
    return fn ? reinterpret_cast<HealthFn>(fn)(entity) : 0.0f;
}

uintptr_t lifeBlock(uintptr_t entity) {
    for (const char* name : kDamageComponents) {
        const uintptr_t component = decima::findComponent(entity, msvc_rtti::vtableOf(name));
        const uintptr_t block = component ? decima::readPointer(component + kLifeBlock) : 0;
        if (block && (ds2::field<uint8_t>(block, kLifeValid) & 1)) return block;
    }
    return 0;
}

}  // namespace

namespace enemy_vitals {

uint8_t readHealth(uintptr_t entity) {
    const float maximum = slotValue(entity, kGetMaxHealthSlot);
    if (maximum <= 0) return enemy_wire::kHealthUnknown;
    return static_cast<uint8_t>(std::clamp(slotValue(entity, kGetHealthSlot) / maximum, 0.0f, 1.0f) * kHealthFull);
}

bool isDead(uintptr_t entity) { return ds2::entityIsDead(entity) || readHealth(entity) == 0; }

uintptr_t healthAddress(uintptr_t entity) {
    const uintptr_t block = lifeBlock(entity);
    return block ? block + kLifeCurrent : 0;
}

void applyHealth(uintptr_t entity, uint8_t health) {
    if (health == enemy_wire::kHealthUnknown) return;
    const uintptr_t block = lifeBlock(entity);
    if (!block) return;
    const float maximum = ds2::field<float>(block, kLifeMax);
    ds2::field<float>(block, kLifeCurrent) = maximum * std::max(static_cast<float>(health) / kHealthFull, kMinimumLeft);
}

}  // namespace enemy_vitals
