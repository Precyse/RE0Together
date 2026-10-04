#include "ds2/weapon_layout.h"

#include <algorithm>

#include "decima/entity.h"
#include "decima/safe_read.h"
#include "ds2/engine.h"

namespace {

bool makesShots(uintptr_t component) {
    const uintptr_t vtable = decima::readPointer(component);
    const uintptr_t slot = vtable ? decima::readPointer(vtable + ds2::weapon::kCreateAttackSlot * sizeof(uintptr_t)) : 0;
    return slot && std::any_of(std::begin(ds2::weapon::kShotFunctions), std::end(ds2::weapon::kShotFunctions),
                               [slot](const ds2::weapon::ShotFunction& shot) { return ds2::at(shot.address) == slot; });
}

}  // namespace

namespace ds2::weapon {

uint16_t weaponId(uintptr_t weapon) {
    uint16_t id = weapon_wire::kHolstered;
    decima::safeRead(weapon + kWeaponId, id);
    return id;
}

uintptr_t weaponOwner(uintptr_t weapon) { return weapon ? weakEntity(weapon + kWeaponOwner) : 0; }

uintptr_t shotBehavior(uintptr_t weapon) { return decima::findComponentWhere(weapon, makesShots); }

}  // namespace ds2::weapon
