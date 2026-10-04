// DEATH STRANDING 2: the local player's weapon for weapon sync. The state is read from the weapon table of Sam's entity
// (ds2/weapon_layout.h): the current entry's weapon, drawn while the entry's drawn flag is set. Shots are caught by
// detours on the CreateAttackRequest of each behavior class that makes one (vtable slot 46, run from the common update
// gate when the fire request byte is set); the detour records the weapon, where it points and the pellet count, then
// lets the engine make the shot.
#include "ds2/local_weapon.h"

#include <cstring>
#include <mutex>
#include <utility>
#include <vector>

#include "decima/safe_read.h"
#include "decima/world_transform.h"
#include "ds2/engine.h"
#include "ds2/place.h"
#include "ds2/player.h"
#include "ds2/weapon_layout.h"
#include "game.h"
#include "hooks.h"
#include "log.h"

namespace {

using ds2::weapon::kShotFunctions;

constexpr size_t kMaxQueuedFires = 64;
constexpr int kForwardRow = 1;  // RotMatrix rows: right, forward, up
// Every weapon is created attached to the right hand (the engine's own call); a left-hand weapon is not told apart yet.
constexpr weapon_wire::Hand kHeldHand = weapon_wire::Hand::Right;
constexpr weapon_wire::WeaponState kHolsteredState{weapon_wire::kHolstered, static_cast<uint8_t>(weapon_wire::Hand::Default), 0};

using ShotFn = void (*)(uintptr_t behavior, float amount);

ShotFn g_original[std::size(kShotFunctions)];

std::mutex g_mutex;  // the simulation thread records shots, the net thread takes them
std::vector<weapon_wire::WeaponFire> g_fires;

// Net thread only: what was last logged about the table, so a change shows once.
struct TableLog {
    uint32_t index = UINT32_MAX;
    uint8_t flags[ds2::weapon::kEntryFlagBytes] = {};
    uint16_t id = 0;
};
TableLog g_logged;

void logTableChange(uint32_t index, const uint8_t (&flags)[ds2::weapon::kEntryFlagBytes], uint16_t id) {
    if (index == g_logged.index && id == g_logged.id && std::memcmp(flags, g_logged.flags, sizeof(flags)) == 0) return;
    g_logged.index = index;
    g_logged.id = id;
    std::memcpy(g_logged.flags, flags, sizeof(flags));
    logger::write("local_weapon: table entry %u, weapon %u, flags %02x %02x %02x %02x %02x %02x %02x", index, id, flags[0],
                  flags[1], flags[2], flags[3], flags[4], flags[5], flags[6]);
}

bool pelletsCounted(weapon_wire::Kind kind) {
    return kind == weapon_wire::Kind::Gun || kind == weapon_wire::Kind::ShotGun;
}

bool describeShot(uintptr_t behavior, weapon_wire::Kind kind, weapon_wire::WeaponFire& out) {
    const uintptr_t weapon = decima::readPointer(behavior + ds2::weapon::kBehaviorWeapon);
    const uintptr_t owner = weapon ? ds2::weapon::weaponOwner(weapon) : 0;
    if (!owner || owner != ds2::localPlayerEntity()) return false;
    decima::WorldTransform at;
    if (!ds2::entityTransform(weapon, at)) return false;
    out = {};
    out.weaponId = ds2::weapon::weaponId(weapon);
    out.kind = static_cast<uint8_t>(kind);
    uint32_t pellets = 0;
    if (pelletsCounted(kind) && decima::safeRead(behavior + ds2::weapon::kBehaviorPellets, pellets) &&
        pellets <= weapon_wire::kMaxPellets) {
        out.pellets = pellets;
    }
    out.origin[0] = static_cast<float>(at.position.x);
    out.origin[1] = static_cast<float>(at.position.y);
    out.origin[2] = static_cast<float>(at.position.z);
    std::memcpy(out.direction, at.orientation.row[kForwardRow], sizeof(out.direction));
    return out.weaponId != weapon_wire::kHolstered;
}

void recordShot(uintptr_t behavior, weapon_wire::Kind kind) {
    weapon_wire::WeaponFire fire;
    if (!describeShot(behavior, kind, fire)) return;
    std::lock_guard lock(g_mutex);
    if (g_fires.size() < kMaxQueuedFires) g_fires.push_back(fire);
}

template <size_t I>
void shotDetour(uintptr_t behavior, float amount) {
    recordShot(behavior, kShotFunctions[I].kind);
    g_original[I](behavior, amount);
}

template <size_t... I>
void installShotHooks(std::index_sequence<I...>) {
    (hooks::install("weapon shot", ds2::at(kShotFunctions[I].address), reinterpret_cast<void*>(&shotDetour<I>),
                    reinterpret_cast<void**>(&g_original[I])),
     ...);
}

}  // namespace

namespace local_weapon {

void installEarly() { installShotHooks(std::make_index_sequence<std::size(kShotFunctions)>{}); }

}  // namespace local_weapon

namespace game {

std::optional<weapon_wire::WeaponState> localWeaponState() {
    using namespace ds2::weapon;
    const uintptr_t sam = ds2::localPlayerEntity();
    const uintptr_t table = sam ? decima::readPointer(sam + kEntityTable) : 0;
    uint32_t index = 0;
    if (!table || !decima::safeRead(table + kTableCurrentIndex, index)) return std::nullopt;
    if (index >= kTableEntries) return kHolsteredState;
    const uintptr_t entry = table + kTableFirstEntry + index * kEntrySize;
    uint8_t flags[kEntryFlagBytes] = {};
    const uintptr_t weapon = decima::readPointer(entry + kEntryWeapon);
    if (!weapon || !decima::safeCopy(flags, entry + kEntryFlags, sizeof(flags))) return kHolsteredState;
    const uint16_t id = weaponId(weapon);
    logTableChange(index, flags, id);
    const bool drawn = flags[kEntryDrawn - kEntryFlags] != 0;
    return drawn ? weapon_wire::WeaponState{id, static_cast<uint8_t>(kHeldHand), 0} : kHolsteredState;
}

std::vector<weapon_wire::WeaponFire> takeLocalFires() {
    std::lock_guard lock(g_mutex);
    std::vector<weapon_wire::WeaponFire> out;
    out.swap(g_fires);
    return out;
}

}  // namespace game
