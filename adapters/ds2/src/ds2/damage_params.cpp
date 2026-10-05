// DEATH STRANDING 2: reading a DamageParams' attack context (see ds2/damage_params.h).
#include "ds2/damage_params.h"

#include "decima/safe_read.h"

namespace {

constexpr uintptr_t kWeakTarget = 0x20;       // a weak pointer to an object points at object + 0x20
constexpr uintptr_t kLinkContext = 0x38;      // the link's weak ref to the context
constexpr uintptr_t kContextAttacker = 0x70;  // the context's weak ref to the attacker

uintptr_t objectOfWeak(uintptr_t weakTarget) { return weakTarget ? weakTarget - kWeakTarget : 0; }

}  // namespace

namespace ds2::damage {

uintptr_t contextOf(uintptr_t params) {
    const uintptr_t link = objectOfWeak(decima::readPointer(params + kParamsLink));
    return link ? objectOfWeak(decima::readPointer(link + kLinkContext)) : 0;
}

uintptr_t attackerOf(uintptr_t params) {
    const uintptr_t context = contextOf(params);
    return context ? objectOfWeak(decima::readPointer(context + kContextAttacker)) : 0;
}

float amountOf(uintptr_t params) {
    float amount = 0, given = 0;
    decima::safeRead(params + kParamsAmount, amount);
    decima::safeRead(params + kParamsGivenAmount, given);
    return amount > 0 ? amount : given;
}

}  // namespace ds2::damage
