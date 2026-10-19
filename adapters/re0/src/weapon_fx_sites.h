#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

// The allow-list of partner_mode=state weapon effects (pure, unit tested): the call sites in the weapon handlers whose
// effect primitive the owner records and the receiver replays (docs/RE0_NOTES.md "State mode: weapon effects", every
// role E row of the call-site table, read and checked in the decrypted image). A recorded call is kept only when its
// primitive was called from one of these return addresses, so the engine's other users of the same primitives (the
// equip step's own wick, bullet bounces and explosions, enemy effects) stay out. Hitscan, melee, ammo, the shot counter
// and the pad rumble are owner-only side effects and have no row.
namespace weapon_fx_sites {

enum class Primitive : uint8_t {
    PlaySe,       // sWeapon::playSe 0x532bb0
    GenericSe,    // generic sound 0x532ce0 (Wesker's attack)
    MuzzleFlash,  // player::muzzleFlash 0x50cb40
    Shoot,        // sWeapon::shoot 0x530ec0: projectile (kind 0), casing (1) or magazine (2)
    Effect,       // sEffect::create 0x55a6d0
    Stow,         // molotov stow 0x50f8b0
    Wick,         // molotov wick 0x50d6e0
    Ripple,       // water ripple sound 0x532880
};

// One allowed call: the address after the call instruction and, for `Shoot`, the muzzle the handler built the matrix
// from just before (0x50e490's second argument); 0 for every other primitive.
struct Site {
    Primitive primitive;
    uintptr_t returnAddress;
    uint8_t muzzle;
};

// The index in this table is the site id on the wire.
constexpr std::array kSites = std::to_array<Site>({
    // sWeapon::playSe: fire, pump, throw, SMG burst, melee swing and hit, reload, magazine, dry fire, type 0x16 sequencer
    {Primitive::PlaySe, 0x504b88, 0},
    {Primitive::PlaySe, 0x504850, 0},
    {Primitive::PlaySe, 0x504572, 0},
    {Primitive::PlaySe, 0x505292, 0},
    {Primitive::PlaySe, 0x504f12, 0},
    {Primitive::PlaySe, 0x504f7e, 0},
    {Primitive::PlaySe, 0x505af9, 0},
    {Primitive::PlaySe, 0x505c85, 0},
    {Primitive::PlaySe, 0x505cc9, 0},
    {Primitive::PlaySe, 0x4f861d, 0},
    {Primitive::PlaySe, 0x53584b, 0},
    // player::muzzleFlash: firearms (0x504770) and projectile throws (0x504320)
    {Primitive::MuzzleFlash, 0x504c8e, 0},
    {Primitive::MuzzleFlash, 0x5044c0, 0},
    // sEffect::create: hunting gun / shotgun flash, SMG flash, Wesker effects, water splash of the two hitscans
    {Primitive::Effect, 0x504d7f, 0},
    {Primitive::Effect, 0x5053cd, 0},
    {Primitive::Effect, 0x5056b3, 0},
    {Primitive::Effect, 0x5056f7, 0},
    {Primitive::Effect, 0x52f84b, 0},
    {Primitive::Effect, 0x530160, 0},
    // sWeapon::shoot: casings (muzzle 1), the projectile (muzzle 0), the reload magazine (muzzle 2)
    {Primitive::Shoot, 0x504ce3, 1},
    {Primitive::Shoot, 0x5048b8, 1},
    {Primitive::Shoot, 0x504500, 0},
    {Primitive::Shoot, 0x50540d, 1},
    {Primitive::Shoot, 0x505aad, 1},
    {Primitive::Shoot, 0x505ad0, 1},
    {Primitive::Shoot, 0x505cfa, 2},
    {Primitive::Shoot, 0x53571f, 1},
    {Primitive::Shoot, 0x53576d, 1},
    // molotov: stow after the throw, light the next wick
    {Primitive::Stow, 0x504525, 0},
    {Primitive::Wick, 0x504467, 0},
    // water ripple sound of the two hitscans
    {Primitive::Ripple, 0x52f87b, 0},
    {Primitive::Ripple, 0x530191, 0},
    // generic sound 0x532ce0: Wesker's attack
    {Primitive::GenericSe, 0x505591, 0},
    {Primitive::GenericSe, 0x5055c9, 0},
    {Primitive::GenericSe, 0x505639, 0},
    {Primitive::GenericSe, 0x505671, 0},
});

constexpr uint8_t kNoSite = 0xff;
static_assert(kSites.size() < kNoSite);

// The site id of a call to `primitive` that returns to `returnAddress`, or kNoSite.
constexpr uint8_t find(Primitive primitive, uintptr_t returnAddress) {
    for (size_t id = 0; id < kSites.size(); ++id) {
        if (kSites[id].primitive == primitive && kSites[id].returnAddress == returnAddress) return static_cast<uint8_t>(id);
    }
    return kNoSite;
}

// The sound ids the engine accepts: sWeapon::playSe asserts outside 0x1e00..0x290c; Wesker's attack plays 0xa54..0xa57.
constexpr uint32_t kPlaySeFirst = 0x1e00;
constexpr uint32_t kPlaySeLast = 0x290c;
constexpr uint32_t kGenericSeFirst = 0xa54;
constexpr uint32_t kGenericSeLast = 0xa57;

constexpr bool soundAllowed(Primitive primitive, uint32_t soundId) {
    if (primitive == Primitive::PlaySe) return soundId >= kPlaySeFirst && soundId <= kPlaySeLast;
    if (primitive == Primitive::GenericSe) return soundId >= kGenericSeFirst && soundId <= kGenericSeLast;
    return false;
}

}  // namespace weapon_fx_sites
