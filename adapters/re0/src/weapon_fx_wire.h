#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <type_traits>
#include <vector>

#include "protocol.h"
#include "weapon_fx_sites.h"

// Wire format of the weapon effects of partner_mode=state (docs/RE0_NOTES.md "State mode: weapon effects"): the effect
// primitives the owner's fire, reload and dry-fire handlers called in one frame (WEAPON_FX), each with the arguments
// that matter and the allow-list site it came from. Pure, no game access.
namespace weapon_fx_wire {

constexpr uint16_t kMsgWeaponFx = proto::kFirstGameType + 0x42;  // 0x0142, owner to all, reliable, numbered

constexpr size_t kMaxEntries = 16;
constexpr uint8_t kMaxCharacter = 1;  // the header's character byte is for logs: Billy 0, Rebecca 1

enum class Op : uint8_t { Se = 1, Flash = 2, Shoot = 3, Effect = 4, Ripple = 5, Stow = 6, Wick = 7 };

constexpr uint8_t kProjectileKind = 0;  // Shoot kinds: the uBullet projectile, a casing, a magazine
constexpr uint8_t kMaxShootKind = 2;
constexpr uint8_t kMaxMuzzle = 2;
constexpr uint8_t kMaxRippleSub = 1;
constexpr float kMaxLevel = 8.0f;           // sound volume and flash scale are 1.0 at every listed site
constexpr float kUnitNormTolerance = 0.05f;  // |q|^2 of a projectile's rotation
constexpr uint8_t kOwnerPlayer = 0;          // Effect owner: the character or its weapon unit
constexpr uint8_t kOwnerWeaponUnit = 1;
constexpr uint8_t kAttachNone = 0;           // Effect attach: nothing, the character or its weapon unit
constexpr uint8_t kAttachPlayer = 1;
constexpr uint8_t kAttachWeaponUnit = 2;
constexpr int8_t kNoJoint = -1;

struct Header {
    uint32_t senderMs;
    uint32_t seq;
    uint8_t character;
    uint8_t count;
    uint16_t pad;
};
static_assert(sizeof(Header) == 12);

struct SeWire {
    uint8_t op;
    uint8_t site;
    uint16_t soundId;
    float volume;
};
static_assert(sizeof(SeWire) == 8);

struct FlashWire {
    uint8_t op;
    uint8_t site;
    uint8_t index;
    uint8_t pad;
    float scale;
};
static_assert(sizeof(FlashWire) == 8);

struct ShootWire {
    uint8_t op;
    uint8_t site;
    uint8_t kind;
    uint8_t muzzle;
};
static_assert(sizeof(ShootWire) == 4);

// A projectile carries the muzzle matrix as position and rotation, the owner's two aim pitches (the launch reads both
// from its owner) and the owner's weapon type (a log line when the puppet holds another one).
struct ProjectileWire {
    ShootWire shoot;
    float pos[3];
    float quat[4];
    float autoPitch;
    float manualPitch;
    uint32_t weapon;
};
static_assert(sizeof(ProjectileWire) == 44);

struct EffectWire {
    uint8_t op;
    uint8_t site;
    uint16_t index;
    uint8_t owner;
    uint8_t attach;
    int8_t joint;
    uint8_t pad;
    float pos[3];
    float rot[3];
};
static_assert(sizeof(EffectWire) == 32);

struct RippleWire {
    uint8_t op;
    uint8_t site;
    uint8_t sub;
    uint8_t pad;
    float pos[3];
};
static_assert(sizeof(RippleWire) == 16);

struct ThrowableWire {
    uint8_t op;
    uint8_t site;
    uint16_t pad;
};
static_assert(sizeof(ThrowableWire) == 4);

// One recorded call. Which fields mean something depends on `op`; the others stay zero.
struct Entry {
    Op op = Op::Se;
    uint8_t site = 0;
    uint16_t number = 0;  // Se: sound id; Effect: effect index
    uint8_t kind = 0;     // Shoot: kind; Flash: effect index; Ripple: sub sound
    uint8_t muzzle = 0;   // Shoot
    uint8_t owner = 0;    // Effect
    uint8_t attach = 0;   // Effect
    int8_t joint = 0;     // Effect
    float level = 0.0f;   // Se: volume; Flash: scale
    float pos[3] = {};    // Shoot (projectile), Effect, Ripple
    float quat[4] = {};   // Shoot (projectile)
    float rot[3] = {};    // Effect
    float autoPitch = 0.0f;    // Shoot (projectile)
    float manualPitch = 0.0f;  // Shoot (projectile)
    uint32_t weapon = 0;       // Shoot (projectile)
};

// The calls of one frame of the owner's character, stamped with the owner's clock.
struct Batch {
    uint32_t senderMs = 0;
    uint32_t seq = 0;
    uint8_t character = 0;
    std::vector<Entry> entries;
};

constexpr Op opOf(weapon_fx_sites::Primitive primitive) {
    using weapon_fx_sites::Primitive;
    switch (primitive) {
        case Primitive::PlaySe:
        case Primitive::GenericSe: return Op::Se;
        case Primitive::MuzzleFlash: return Op::Flash;
        case Primitive::Shoot: return Op::Shoot;
        case Primitive::Effect: return Op::Effect;
        case Primitive::Ripple: return Op::Ripple;
        case Primitive::Stow: return Op::Stow;
        case Primitive::Wick: return Op::Wick;
    }
    return Op::Se;
}

inline const char* name(Op op) {
    switch (op) {
        case Op::Se: return "sound";
        case Op::Flash: return "muzzle flash";
        case Op::Shoot: return "shoot";
        case Op::Effect: return "effect";
        case Op::Ripple: return "ripple";
        case Op::Stow: return "stow";
        case Op::Wick: return "wick";
    }
    return "unknown";
}

inline bool isProjectile(const Entry& entry) { return entry.op == Op::Shoot && entry.kind == kProjectileKind; }

inline size_t sizeOf(const Entry& entry) {
    switch (entry.op) {
        case Op::Se: return sizeof(SeWire);
        case Op::Flash: return sizeof(FlashWire);
        case Op::Shoot: return isProjectile(entry) ? sizeof(ProjectileWire) : sizeof(ShootWire);
        case Op::Effect: return sizeof(EffectWire);
        case Op::Ripple: return sizeof(RippleWire);
        case Op::Stow:
        case Op::Wick: return sizeof(ThrowableWire);
    }
    return 0;
}

template <typename Wire>
void append(std::vector<uint8_t>& out, const Wire& wire) {
    static_assert(std::is_trivially_copyable_v<Wire>);
    const auto* bytes = reinterpret_cast<const uint8_t*>(&wire);
    out.insert(out.end(), bytes, bytes + sizeof(Wire));
}

inline void appendEntry(std::vector<uint8_t>& out, const Entry& entry) {
    const uint8_t op = static_cast<uint8_t>(entry.op);
    switch (entry.op) {
        case Op::Se: append(out, SeWire{op, entry.site, entry.number, entry.level}); break;
        case Op::Flash: append(out, FlashWire{op, entry.site, entry.kind, 0, entry.level}); break;
        case Op::Shoot: {
            const ShootWire shoot{op, entry.site, entry.kind, entry.muzzle};
            if (!isProjectile(entry)) {
                append(out, shoot);
                break;
            }
            ProjectileWire wire{shoot, {}, {}, entry.autoPitch, entry.manualPitch, entry.weapon};
            std::memcpy(wire.pos, entry.pos, sizeof(wire.pos));
            std::memcpy(wire.quat, entry.quat, sizeof(wire.quat));
            append(out, wire);
            break;
        }
        case Op::Effect: {
            EffectWire wire{op, entry.site, entry.number, entry.owner, entry.attach, entry.joint, 0, {}, {}};
            std::memcpy(wire.pos, entry.pos, sizeof(wire.pos));
            std::memcpy(wire.rot, entry.rot, sizeof(wire.rot));
            append(out, wire);
            break;
        }
        case Op::Ripple: {
            RippleWire wire{op, entry.site, entry.kind, 0, {}};
            std::memcpy(wire.pos, entry.pos, sizeof(wire.pos));
            append(out, wire);
            break;
        }
        case Op::Stow:
        case Op::Wick: append(out, ThrowableWire{op, entry.site, 0}); break;
    }
}

// The payload of `batch`, or nothing when it holds no entry or more than kMaxEntries.
inline std::optional<std::vector<uint8_t>> encode(const Batch& batch) {
    if (batch.entries.empty() || batch.entries.size() > kMaxEntries) return std::nullopt;
    std::vector<uint8_t> out;
    append(out, Header{batch.senderMs, batch.seq, batch.character, static_cast<uint8_t>(batch.entries.size()), 0});
    for (const Entry& entry : batch.entries) appendEntry(out, entry);
    return out;
}

inline bool finite(float value) { return std::isfinite(value); }

template <size_t N>
bool allFinite(const float (&values)[N]) {
    for (const float value : values) {
        if (!finite(value)) return false;
    }
    return true;
}

inline bool levelOk(float level) { return finite(level) && level >= 0.0f && level <= kMaxLevel; }

inline bool unitQuaternion(const float (&quat)[4]) {
    if (!allFinite(quat)) return false;
    const float norm = quat[0] * quat[0] + quat[1] * quat[1] + quat[2] * quat[2] + quat[3] * quat[3];
    return std::fabs(norm - 1.0f) <= kUnitNormTolerance;
}

template <typename Wire>
bool take(std::span<const uint8_t>& rest, Wire& wire) {
    if (rest.size() < sizeof(Wire)) return false;
    std::memcpy(&wire, rest.data(), sizeof(Wire));
    rest = rest.subspan(sizeof(Wire));
    return true;
}

// Reads one entry whose first byte is the op; false on any refusal.
inline bool takeEntry(std::span<const uint8_t>& rest, Entry& out) {
    if (rest.empty() || rest[0] < static_cast<uint8_t>(Op::Se) || rest[0] > static_cast<uint8_t>(Op::Wick)) return false;
    const Op op = static_cast<Op>(rest[0]);
    out = Entry{};
    out.op = op;
    const auto siteOk = [&](uint8_t site) {
        return site < weapon_fx_sites::kSites.size() && opOf(weapon_fx_sites::kSites[site].primitive) == op;
    };
    switch (op) {
        case Op::Se: {
            SeWire wire;
            if (!take(rest, wire) || !siteOk(wire.site) || !levelOk(wire.volume) ||
                !weapon_fx_sites::soundAllowed(weapon_fx_sites::kSites[wire.site].primitive, wire.soundId)) {
                return false;
            }
            out.site = wire.site;
            out.number = wire.soundId;
            out.level = wire.volume;
            return true;
        }
        case Op::Flash: {
            FlashWire wire;
            if (!take(rest, wire) || !siteOk(wire.site) || !levelOk(wire.scale)) return false;
            out.site = wire.site;
            out.kind = wire.index;
            out.level = wire.scale;
            return true;
        }
        case Op::Shoot: {
            if (rest.size() < sizeof(ShootWire)) return false;
            const bool projectile = rest[offsetof(ShootWire, kind)] == kProjectileKind;
            ProjectileWire wire{};
            if (projectile ? !take(rest, wire) : !take(rest, wire.shoot)) return false;
            const ShootWire& shoot = wire.shoot;
            if (!siteOk(shoot.site) || shoot.kind > kMaxShootKind || shoot.muzzle > kMaxMuzzle) return false;
            out.site = shoot.site;
            out.kind = shoot.kind;
            out.muzzle = shoot.muzzle;
            if (!projectile) return true;
            if (!allFinite(wire.pos) || !unitQuaternion(wire.quat) || !finite(wire.autoPitch) || !finite(wire.manualPitch)) return false;
            std::memcpy(out.pos, wire.pos, sizeof(out.pos));
            std::memcpy(out.quat, wire.quat, sizeof(out.quat));
            out.autoPitch = wire.autoPitch;
            out.manualPitch = wire.manualPitch;
            out.weapon = wire.weapon;
            return true;
        }
        case Op::Effect: {
            EffectWire wire;
            if (!take(rest, wire) || !siteOk(wire.site) || wire.owner > kOwnerWeaponUnit || wire.attach > kAttachWeaponUnit ||
                wire.joint < kNoJoint || !allFinite(wire.pos) || !allFinite(wire.rot)) {
                return false;
            }
            out.site = wire.site;
            out.number = wire.index;
            out.owner = wire.owner;
            out.attach = wire.attach;
            out.joint = wire.joint;
            std::memcpy(out.pos, wire.pos, sizeof(out.pos));
            std::memcpy(out.rot, wire.rot, sizeof(out.rot));
            return true;
        }
        case Op::Ripple: {
            RippleWire wire;
            if (!take(rest, wire) || !siteOk(wire.site) || wire.sub > kMaxRippleSub || !allFinite(wire.pos)) return false;
            out.site = wire.site;
            out.kind = wire.sub;
            std::memcpy(out.pos, wire.pos, sizeof(out.pos));
            return true;
        }
        case Op::Stow:
        case Op::Wick: {
            ThrowableWire wire;
            if (!take(rest, wire) || !siteOk(wire.site)) return false;
            out.site = wire.site;
            return true;
        }
    }
    return false;
}

// True when `entry` survives a round trip through the receiver's own rules, so the owner never sends a message the
// peer would refuse as a whole.
inline bool acceptable(const Entry& entry) {
    std::vector<uint8_t> bytes;
    appendEntry(bytes, entry);
    std::span<const uint8_t> rest = bytes;
    Entry parsed;
    return takeEntry(rest, parsed) && rest.empty();
}

// A received WEAPON_FX, or nothing when it is shorter than its count says, longer than its entries, has an unknown op,
// a site that does not belong to its op, a value the engine would assert on, or a non-finite number.
inline std::optional<Batch> decode(std::span<const uint8_t> payload) {
    Header header;
    std::span<const uint8_t> rest = payload;
    if (!take(rest, header) || header.count == 0 || header.count > kMaxEntries || header.character > kMaxCharacter) {
        return std::nullopt;
    }
    Batch batch;
    batch.senderMs = header.senderMs;
    batch.seq = header.seq;
    batch.character = header.character;
    batch.entries.resize(header.count);
    for (Entry& entry : batch.entries) {
        if (!takeEntry(rest, entry)) return std::nullopt;
    }
    if (!rest.empty()) return std::nullopt;
    return batch;
}

}  // namespace weapon_fx_wire
