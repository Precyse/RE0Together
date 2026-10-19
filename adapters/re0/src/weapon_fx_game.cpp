#include "weapon_fx_game.h"

#include <windows.h>

#include "crash_dump.h"
#include "game.h"
#include "weapon_fx_matrix.h"
#include "weapon_fx_sites.h"

namespace {

using weapon_fx_sites::Primitive;
using weapon_fx_wire::Entry;
using weapon_fx_wire::Op;

constexpr uint32_t kNoExtra = 0;  // the last argument of sEffect::create is 0 at every listed site
constexpr size_t kEffectHandleWords = game::kEffectHandleBytes / sizeof(uint32_t);

void* weaponObject() { return reinterpret_cast<void*>(game::readPointer(game::kWeaponGlobal)); }

bool playSound(uintptr_t player, const Entry& entry) {
    const Primitive primitive = weapon_fx_sites::kSites[entry.site].primitive;
    if (primitive == Primitive::GenericSe) {
        reinterpret_cast<weapon_fx_game::GenericSeFunction>(game::kWeaponGenericSeFunction)(player, entry.number, entry.level);
        return true;
    }
    void* weapon = weaponObject();
    if (!weapon) return false;
    reinterpret_cast<weapon_fx_game::PlaySeFunction>(game::kWeaponPlaySeFunction)(weapon, nullptr, player, entry.number, entry.level);
    return true;
}

bool shoot(uintptr_t player, const float (&matrix)[weapon_fx_matrix::kFloats], uint32_t kind) {
    void* weapon = weaponObject();
    if (!weapon) return false;
    reinterpret_cast<weapon_fx_game::ShootFunction>(game::kWeaponShootFunction)(weapon, nullptr, player, matrix, kind);
    return true;
}

// A casing or a magazine leaves the puppet's own weapon at the muzzle the owner's handler used.
bool shootFromMuzzle(uintptr_t player, const Entry& entry) {
    alignas(16) float matrix[weapon_fx_matrix::kFloats];
    game::callThiscall<float*>(game::kMuzzleMatrixFunction, reinterpret_cast<void*>(player), matrix,
                               static_cast<uint32_t>(entry.muzzle));
    return shoot(player, matrix, entry.kind);
}

// The projectile launch reads both aim pitches from its owner, so the puppet gets the owner's before the call.
bool shootProjectile(uintptr_t player, const Entry& entry) {
    game::writeMemory(player + game::kPlayerAutoPitchOffset, entry.autoPitch);
    game::writeMemory(player + game::kPlayerManualPitchOffset, entry.manualPitch);
    alignas(16) float matrix[weapon_fx_matrix::kFloats];
    weapon_fx_matrix::join(entry.pos, entry.quat, matrix);
    return shoot(player, matrix, weapon_fx_wire::kProjectileKind);
}

bool createEffect(uintptr_t player, const Entry& entry) {
    const uintptr_t unit = weapon_fx_game::weaponUnitOf(player);
    const uintptr_t owner = entry.owner == weapon_fx_wire::kOwnerPlayer ? player : unit;
    uintptr_t attach = 0;
    if (entry.attach == weapon_fx_wire::kAttachPlayer) attach = player;
    if (entry.attach == weapon_fx_wire::kAttachWeaponUnit) attach = unit;
    void* effects = reinterpret_cast<void*>(game::readPointer(game::kEffectGlobal));
    const bool needsUnit = entry.owner == weapon_fx_wire::kOwnerWeaponUnit || entry.attach == weapon_fx_wire::kAttachWeaponUnit;
    if (!effects || !owner || (needsUnit && !unit)) return false;
    uint32_t flags[2] = {};
    game::readMemory(game::kEffectFlagsGlobal, flags[0]);
    flags[1] = game::kEffectFlagsSecond;
    uint32_t handle[kEffectHandleWords] = {};
    reinterpret_cast<weapon_fx_game::EffectCreateFunction>(game::kEffectCreateFunction)(
        effects, nullptr, handle, owner, game::readPointer(owner + game::kEffectSetOffset), entry.number, attach, entry.joint,
        entry.pos, entry.rot, flags, kNoExtra);
    return true;
}

bool callPlayer(uintptr_t function, uintptr_t player) {
    reinterpret_cast<weapon_fx_game::PlayerFunction>(function)(reinterpret_cast<void*>(player), nullptr);
    return true;
}

bool replayUnguarded(uintptr_t player, const Entry& entry) {
    switch (entry.op) {
        case Op::Se: return playSound(player, entry);
        case Op::Flash:
            reinterpret_cast<weapon_fx_game::MuzzleFlashFunction>(game::kMuzzleFlashFunction)(reinterpret_cast<void*>(player), nullptr,
                                                                                         entry.kind, entry.level);
            return true;
        case Op::Shoot: return weapon_fx_wire::isProjectile(entry) ? shootProjectile(player, entry) : shootFromMuzzle(player, entry);
        case Op::Effect: return createEffect(player, entry);
        case Op::Ripple:
            reinterpret_cast<weapon_fx_game::RippleFunction>(game::kWaterRippleFunction)(player, entry.kind, entry.pos);
            return true;
        case Op::Stow: return callPlayer(game::kStowThrowableFunction, player);
        case Op::Wick: return callPlayer(game::kLightWickFunction, player);
    }
    return false;
}

}  // namespace

namespace weapon_fx_game {

uintptr_t weaponUnitOf(uintptr_t player) { return game::readPointer(player + game::kPlayerWeaponUnitOffset); }

int32_t weaponTypeOf(uintptr_t player) {
    int32_t type = 0;
    game::readMemory(player + game::kPlayerWeaponTypeOffset, type);
    return type;
}

bool replay(uintptr_t player, const Entry& entry) {
    __try {
        return replayUnguarded(player, entry);
    } __except (crash_dump::reportCaught(GetExceptionInformation(), "weapon_fx_game: replay")) {
        return false;
    }
}

}  // namespace weapon_fx_game
