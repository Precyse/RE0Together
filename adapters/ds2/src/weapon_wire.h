#pragma once
// WEAPON_STATE and WEAPON_FIRE: what weapon the local player has in hand, and each shot or throw it makes, sent to the
// partner so the partner's body of this player holds the same weapon and plays the same shot (docs/DS2_NOTES.md,
// "Weapon sync"). Both are reliable, so a shot never overtakes the draw that precedes it. The real damage of a shot
// travels through the combat messages; a shot played from this wire is cosmetic.
#include <cmath>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

#include "protocol.h"

namespace weapon_wire {

constexpr uint16_t kMsgWeaponState = proto::kFirstGameType + 0x24;  // 0x0124, to all, reliable: WeaponState, on change
constexpr uint16_t kMsgWeaponFire = proto::kFirstGameType + 0x25;   // 0x0125, to all, reliable: WeaponFire, one per shot

constexpr uint16_t kHolstered = 0;  // WeaponState::weaponId when no weapon is drawn
constexpr uint32_t kMaxPellets = 64;

// EDSWeaponAttachPoint: the hand a weapon is held in.
enum class Hand : uint8_t { Default = 0, Right = 1, Left = 2 };
constexpr uint8_t kLastHand = static_cast<uint8_t>(Hand::Left);

// Which weapon behavior made the shot (the engine's CreateAttackRequest of that behavior class, vtable slot 46).
// Sniper rifles, revolvers and the ghost mech's machine gun share Gun's; mech baggage shares HandGrenade's.
enum class Kind : uint8_t { Gun = 1, ShotGun, BolaGun, StickyGun, GrenadeLauncher, HandGrenade, SingleShotBeam };
constexpr uint8_t kLastKind = static_cast<uint8_t>(Kind::SingleShotBeam);

struct WeaponState {
    uint16_t weaponId;  // EDSWeaponId, kHolstered when nothing is drawn
    uint8_t hand;       // Hand
    uint8_t reserved;

    bool operator==(const WeaponState&) const = default;
};
static_assert(sizeof(WeaponState) == 4);

struct WeaponFire {
    uint16_t weaponId;
    uint8_t kind;  // Kind
    uint8_t reserved;
    uint32_t pellets;      // Gun and ShotGun: the weapon's pellet count for this shot, else 0
    float origin[3];       // the weapon's world position, metres
    float direction[3];    // the weapon's forward axis, unit length
};
static_assert(sizeof(WeaponFire) == 32);

inline bool finite3(const float (&v)[3]) { return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]); }

template <class T>
std::vector<uint8_t> encode(const T& message) {
    const auto bytes = proto::bytesOf(message);
    return {bytes.begin(), bytes.end()};
}

// False for a payload of the wrong size, an unknown hand or an out-of-range value; `out` is then untouched.
inline bool decode(std::span<const uint8_t> payload, WeaponState& out) {
    if (payload.size() != sizeof(WeaponState)) return false;
    WeaponState state;
    std::memcpy(&state, payload.data(), sizeof(state));
    if (state.hand > kLastHand) return false;
    out = state;
    return true;
}

inline bool decode(std::span<const uint8_t> payload, WeaponFire& out) {
    if (payload.size() != sizeof(WeaponFire)) return false;
    WeaponFire fire;
    std::memcpy(&fire, payload.data(), sizeof(fire));
    if (fire.weaponId == kHolstered || fire.kind == 0 || fire.kind > kLastKind || fire.pellets > kMaxPellets) return false;
    if (!finite3(fire.origin) || !finite3(fire.direction)) return false;
    out = fire;
    return true;
}

}  // namespace weapon_wire
