#pragma once
// DS2-internal: reading the attack context behind a DamageParams (docs/DS2_NOTES.md, "Enemy combat"). params +0x20 is a
// weak ref to the attack event link, whose +0x38 is a weak ref to the attack context; the context holds the attacker
// (a weak ref at +0x70, the one ApplyDamage reads) and a data block at +0x120 with the attack id (+0x08), the attack
// type (+0x0C), the attack resource (+0x18) and the source entity id (+0x20). Any thread; every read is checked.
#include <cstdint>

namespace ds2::damage {

constexpr uintptr_t kParamsLink = 0x20;
constexpr uintptr_t kParamsAmount = 0x78;         // the amount ApplyDamage uses; 0 means "take the attacker's damage value"
constexpr uintptr_t kParamsGivenAmount = 0x7C;    // the amount the DamageParams constructor was given
constexpr uintptr_t kContextData = 0x120;
constexpr uintptr_t kDataId = 0x08, kDataType = 0x0C, kDataResource = 0x18, kDataSource = 0x20;

// The attack context of a hit (0 when it has none).
uintptr_t contextOf(uintptr_t params);

// The attacking entity of a hit (0 when it has none).
uintptr_t attackerOf(uintptr_t params);

// The damage of a hit: +0x78 when set, else the constructor's amount at +0x7C (a real weapon hit leaves +0x78 at 0 and
// ApplyDamage works the value out itself, so +0x78 alone reads as 0 for it).
float amountOf(uintptr_t params);

}  // namespace ds2::damage
