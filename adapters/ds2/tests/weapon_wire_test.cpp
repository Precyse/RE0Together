// weapon_wire: the WEAPON_STATE and WEAPON_FIRE payload checks (no game).
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "../src/weapon_wire.h"

namespace {

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_failures;
}

weapon_wire::WeaponFire shot() {
    weapon_wire::WeaponFire fire{};
    fire.weaponId = 0x7b;
    fire.kind = static_cast<uint8_t>(weapon_wire::Kind::ShotGun);
    fire.pellets = 8;
    fire.origin[0] = 1234.5f;
    fire.origin[1] = -88.25f;
    fire.origin[2] = 301.0f;
    fire.direction[1] = 1.0f;
    return fire;
}

}  // namespace

int main() {
    using namespace weapon_wire;

    const WeaponState drawn{0x8a, static_cast<uint8_t>(Hand::Right), 0};
    WeaponState gotState{};
    check(decode(encode(drawn), gotState) && gotState == drawn, "a drawn weapon round trips");
    const WeaponState holstered{kHolstered, static_cast<uint8_t>(Hand::Default), 0};
    check(decode(encode(holstered), gotState) && gotState == holstered, "a holstered state round trips");

    std::vector<uint8_t> stateBytes = encode(drawn);
    stateBytes.pop_back();
    check(!decode(stateBytes, gotState), "a short state is rejected");
    stateBytes = encode(drawn);
    stateBytes.push_back(0);
    check(!decode(stateBytes, gotState), "a long state is rejected");
    check(!decode(encode(WeaponState{0x8a, static_cast<uint8_t>(kLastHand + 1), 0}), gotState), "an unknown hand is rejected");
    gotState = drawn;
    check(!decode(encode(WeaponState{0x7c, 9, 0}), gotState) && gotState == drawn, "a rejected state leaves the output alone");

    const WeaponFire fire = shot();
    WeaponFire gotFire{};
    check(decode(encode(fire), gotFire) && std::memcmp(&gotFire, &fire, sizeof(fire)) == 0, "a shot round trips");

    std::vector<uint8_t> fireBytes = encode(fire);
    fireBytes.pop_back();
    check(!decode(fireBytes, gotFire), "a short shot is rejected");
    fireBytes = encode(fire);
    fireBytes.push_back(0);
    check(!decode(fireBytes, gotFire), "a long shot is rejected");

    WeaponFire bad = fire;
    bad.weaponId = kHolstered;
    check(!decode(encode(bad), gotFire), "a shot of no weapon is rejected");
    bad = fire;
    bad.kind = 0;
    check(!decode(encode(bad), gotFire), "kind zero is rejected");
    bad.kind = kLastKind + 1;
    check(!decode(encode(bad), gotFire), "an unknown kind is rejected");
    bad = fire;
    bad.pellets = kMaxPellets + 1;
    check(!decode(encode(bad), gotFire), "too many pellets are rejected");
    bad.pellets = kMaxPellets;
    check(decode(encode(bad), gotFire), "the pellet limit itself is accepted");
    bad = fire;
    bad.origin[2] = std::nanf("");
    check(!decode(encode(bad), gotFire), "a NaN origin is rejected");
    bad = fire;
    bad.direction[0] = HUGE_VALF;
    check(!decode(encode(bad), gotFire), "an infinite direction is rejected");

    check(kMsgWeaponState == 0x0124 && kMsgWeaponFire == 0x0125, "the message ids are 0x0124 and 0x0125");

    std::printf(g_failures ? "%d FAILED\n" : "all passed\n", g_failures);
    return g_failures ? 1 : 0;
}
