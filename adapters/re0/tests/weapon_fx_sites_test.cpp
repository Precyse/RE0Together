// Checks the weapon effect allow-list (no game): every listed return address maps to its own site and primitive, other
// addresses and other primitives at a listed address do not, no shot / ammo / counter / rumble site is listed, and the
// engine's sound ranges. Exit 0 when every check passes.
#include <array>
#include <cstdio>

#include "../src/weapon_fx_sites.h"

namespace {

namespace sites = weapon_fx_sites;
using sites::Primitive;

int g_failures = 0;

void check(bool ok, const char* what) {
    if (ok) return;
    std::printf("FAIL: %s\n", what);
    ++g_failures;
}

// The return addresses of the owner-only calls in the weapon handlers (hitscan, wide hitscan, melee hit, Wesker's beam,
// ammo spend, reload, shots counter): they must never be listed.
constexpr std::array kOwnerOnlyReturns = std::to_array<uintptr_t>(
    {0x504aea, 0x504b2c, 0x505248, 0x504f42, 0x505529, 0x504d90, 0x504650, 0x50541e, 0x505961, 0x504b3e, 0x504512});

// Calls into the same primitives from elsewhere in the engine (the equip step's own wick, a return inside a handler
// that is no listed call).
constexpr std::array kUnlistedReturns = std::to_array<uintptr_t>({0x504a5e, 0x504570, 0x4f861e, 0x1, 0x0});

void testEverySiteMaps() {
    for (size_t id = 0; id < sites::kSites.size(); ++id) {
        const sites::Site& site = sites::kSites[id];
        check(sites::find(site.primitive, site.returnAddress) == id, "a listed return address maps to its own site id");
    }
}

void testUnique() {
    for (size_t a = 0; a < sites::kSites.size(); ++a) {
        for (size_t b = a + 1; b < sites::kSites.size(); ++b) {
            check(sites::kSites[a].returnAddress != sites::kSites[b].returnAddress, "no return address is listed twice");
        }
    }
}

void testOtherPrimitiveDoesNotMap() {
    for (const sites::Site& site : sites::kSites) {
        const Primitive other = site.primitive == Primitive::PlaySe ? Primitive::Shoot : Primitive::PlaySe;
        check(sites::find(other, site.returnAddress) == sites::kNoSite, "a listed address does not map for another primitive");
    }
}

void testUnlistedDoNotMap() {
    constexpr std::array primitives = {Primitive::PlaySe, Primitive::GenericSe, Primitive::MuzzleFlash, Primitive::Shoot,
                                       Primitive::Effect, Primitive::Stow,      Primitive::Wick,        Primitive::Ripple};
    for (const Primitive primitive : primitives) {
        for (const uintptr_t address : kUnlistedReturns) check(sites::find(primitive, address) == sites::kNoSite, "an unlisted address does not map");
        for (const uintptr_t address : kOwnerOnlyReturns) check(sites::find(primitive, address) == sites::kNoSite, "an owner-only call is not listed");
    }
}

void testMuzzles() {
    size_t projectiles = 0;
    for (const sites::Site& site : sites::kSites) {
        if (site.primitive != Primitive::Shoot) {
            check(site.muzzle == 0, "only a shoot site names a muzzle");
            continue;
        }
        check(site.muzzle <= 2, "a shoot site's muzzle is one of the weapon's three");
        projectiles += site.muzzle == 0;
    }
    check(projectiles == 1, "exactly one shoot site throws the projectile");
}

void testSounds() {
    check(sites::soundAllowed(Primitive::PlaySe, 0x1e00) && sites::soundAllowed(Primitive::PlaySe, 0x290c), "the weapon sound range is accepted");
    check(!sites::soundAllowed(Primitive::PlaySe, 0x1dff) && !sites::soundAllowed(Primitive::PlaySe, 0x290d), "outside the weapon sound range is refused");
    check(sites::soundAllowed(Primitive::GenericSe, 0xa54) && sites::soundAllowed(Primitive::GenericSe, 0xa57), "Wesker's sounds are accepted");
    check(!sites::soundAllowed(Primitive::GenericSe, 0x1f00) && !sites::soundAllowed(Primitive::GenericSe, 0xa58), "other sounds are refused at Wesker's site");
    check(!sites::soundAllowed(Primitive::Shoot, 0x1f00), "a primitive that plays no sound allows none");
}

}  // namespace

int main() {
    testEverySiteMaps();
    testUnique();
    testOtherPrimitiveDoesNotMap();
    testUnlistedDoNotMap();
    testMuzzles();
    testSounds();
    if (g_failures == 0) std::printf("weapon_fx_sites_test: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
