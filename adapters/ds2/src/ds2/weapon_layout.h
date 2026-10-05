#pragma once
// DS2-internal: how the engine lays out a player's weapons, shared by the local side (reading what Sam holds, hooking his
// shots) and the remote side (the partner's weapon). Static analysis only (tools/ds2/out/analysis/WEAPONS.md); every
// offset marked "live" is still to be confirmed in the running game.
#include <cstddef>
#include <cstdint>

#include "weapon_wire.h"

namespace ds2::weapon {

// The weapon table: DSPlayerEntity +0x56D0 is a component of the player (+0x48 = the entity) that owns 0x60 entries of
// 0x40 bytes from +0xC0, and the index of the current entry at +0x18C0.
constexpr uintptr_t kEntityTable = 0x56D0;
constexpr uintptr_t kTableOwner = 0x48;
constexpr uintptr_t kTableFirstEntry = 0xC0;
constexpr uintptr_t kTableCurrentIndex = 0x18C0;
constexpr uintptr_t kTableRequestedIndex = 0x18C4;  // the entry the table is moving to; live: equal to the current one while a weapon stays drawn
constexpr uint32_t kTableEntries = 0x60;
constexpr size_t kEntrySize = 0x40;
constexpr uintptr_t kEntryFlags = 0x10;   // 7 bytes of state; live: byte +0x11 is non-zero while the weapon is drawn
constexpr size_t kEntryFlagBytes = 7;
constexpr uintptr_t kEntryDrawn = 0x11;
constexpr uintptr_t kEntryWeapon = 0x20;  // the DSWeaponEntity, 0 when the entry holds none
constexpr uintptr_t kEntryTable = 0x38;   // the table component the weapon's events report to

// DSWeaponEntity.
constexpr uintptr_t kWeaponId = 0x2188;    // u16 EDSWeaponId
constexpr uintptr_t kWeaponOwner = 0x338;  // weak pointer to the owner entity
// The ammo a Gun-family behavior shoots with: [behavior +0x5B0] points at three objects (at +0x8, +0x10 and +0x18) that each
// have a u16 id at +0x20. The shot request carries the ids at +0x72, +0x74 and +0x76 (slot 84, 0x14201e910) and the bullet's
// attack type, the type of the damage hit it makes on impact (0x141fe49da), is the third. Static; check live against the
// attack types the combat log shows.
constexpr uintptr_t kBehaviorAmmoSet = 0x5B0;
constexpr uintptr_t kAmmoSetEntries[3] = {0x8, 0x10, 0x18};
constexpr uintptr_t kAmmoId = 0x20;
constexpr size_t kBulletTypeEntry = 2;  // the entry whose id is the bullet's attack type
constexpr uintptr_t kWeaponEntityFlags = 0x98;  // Entity flags; live: bit 1 and bit 16 differ between a drawn and a holstered weapon

// A weapon's behavior component (DSWeaponBehaviorComponent and its subclasses).
constexpr uintptr_t kBehaviorWeapon = 0x50;        // the DSWeaponEntity it belongs to
constexpr uintptr_t kBehaviorPellets = 0x5BC;      // u32, Gun and ShotGun
constexpr uintptr_t kBehaviorFireRequest = 0x4D1;  // byte: the update runs the shot when it is set, then clears it
// The aim target a Gun behavior's shot request reads (slot 84, 0x14201e910): [behavior +0x390] points at a struct whose
// byte +0 bit 0 says a target is set and whose doubles at +0x8, +0x10 and +0x18 are the target's world position; the shot
// goes from the muzzle toward it, and from the muzzle's own forward axis when no target is set (static: the body's owner
// sets none).
constexpr uintptr_t kBehaviorAimTarget = 0x390;
constexpr uintptr_t kAimFlags = 0x00, kAimPosition = 0x08;
constexpr size_t kCreateAttackSlot = 46;           // CreateAttackRequest(float), the shot of every behavior class

// The CreateAttackRequest of each behavior class that makes a shot or a throw (file VAs).
struct ShotFunction {
    uintptr_t address;
    weapon_wire::Kind kind;
};
inline constexpr ShotFunction kShotFunctions[] = {
    {0x14201e800, weapon_wire::Kind::Gun},
    {0x141ff4370, weapon_wire::Kind::ShotGun},
    {0x142007250, weapon_wire::Kind::BolaGun},
    {0x141ff7d00, weapon_wire::Kind::StickyGun},
    {0x14201ded0, weapon_wire::Kind::GrenadeLauncher},
    {0x14201f260, weapon_wire::Kind::HandGrenade},
    {0x141ff6120, weapon_wire::Kind::SingleShotBeam},
};

// Reads of a DSWeaponEntity (kHolstered / 0 when unreadable).
uint16_t weaponId(uintptr_t weapon);
uintptr_t weaponOwner(uintptr_t weapon);

// The three ammo ids of a shot behavior (0 where an object is missing).
void ammoIds(uintptr_t behavior, uint16_t (&ids)[3]);

// The attack type of the damage hit the behavior's bullets make (0 when unknown).
uint16_t bulletAttackType(uintptr_t behavior);

// The weapon's behavior component that makes shots (one whose slot 46 is in kShotFunctions), or 0.
uintptr_t shotBehavior(uintptr_t weapon);

}  // namespace ds2::weapon
