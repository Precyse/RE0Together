#pragma once
// Dragon's Dogma 2 wire messages, adapter to adapter through the launchers (0x0200-0x02FF, DD2 design adapter.md 5;
// docs/CONTRACT.md lists them). Integers little-endian, structs packed; decoders check the exact size and ranges.
#include <cmath>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <utility>
#include <string>
#include <vector>

#include "roster_rule.h"

namespace wire_dd2 {

constexpr uint16_t kProto = 1;

enum Type : uint16_t {
    kGameHello = 0x0200,
    kPlace = 0x0201,
    kBodyCard = 0x0203,
    kBodyState = 0x0210,
    kActionEvent = 0x0211,
    kEnemyRoster = 0x0230,
    kEnemyState = 0x0231,
    kEnemyHit = 0x0232,
    kEnemySpawn = 0x0233,
    kEnemyWant = 0x0234,
    kClock = 0x0240,
};

constexpr uint8_t kPlaces = 8;  // world_gate_rule::Place values 0..7

enum ModuleBit : uint16_t {
    kModuleWorldGate = 1,
    kModuleBody = 2,
    kModuleClock = 4,
    kModuleOffline = 8,
    kModulePuppet = 16,
    kModuleProfile = 32,
    kModulePlagueOff = 64,
    kModuleRoster = 128,
    kModuleEnemySync = 256,
};

constexpr uint8_t kKindArisen = 0, kKindMainPawn = 1;
constexpr uint8_t kBodyKinds = 2;

enum BodyFlag : uint8_t { kHeld = 1, kAir = 2, kClimb = 4, kCaught = 8, kCarry = 16, kDead = 32, kDown = 64, kSwim = 128 };

enum ClockFlag : uint8_t { kClockLocked = 1 };

#pragma pack(push, 1)
struct GameHello {
    uint32_t peTimestamp;
    uint32_t tdbHash;
    uint32_t namesHash;
    uint16_t proto;
    uint16_t moduleMask;
};
struct Place {
    uint8_t place;
    int8_t loadSlot;  // the save slot of the sender's newest game load, -1 before one
    uint16_t loadSerial;
    uint32_t worldEpoch;
    uint32_t stateStoreSerial;
    uint32_t profileCommitSerial;
};
struct BodyState {
    uint32_t seq;
    uint64_t senderUs;
    uint8_t kind;
    uint8_t flags;
    uint16_t loadSerial;
    double position[3];
    float yaw;
    float speedType;
    uint32_t actionHash[2];  // layers 0 and 1
};
struct ActionEvent {
    uint32_t seq;
    uint64_t senderUs;
    uint8_t kind;
    uint8_t layer;
    uint8_t priority;
    uint8_t flags;
    uint32_t actionHash;
    uint32_t reserved;
};
struct Clock {
    double elapsedDaySeconds;
    uint32_t day;
    float timeScale;
    uint32_t serial;
    uint8_t flags;
    uint8_t reserved[3];
};
#pragma pack(pop)
static_assert(sizeof(GameHello) == 16);
static_assert(sizeof(Place) == 16);
static_assert(sizeof(BodyState) == 56);
static_assert(sizeof(ActionEvent) == 24);
static_assert(sizeof(Clock) == 24);

// BODY_CARD: this header, then `count` entries {u16 tag, u16 length, bytes}; `length` counts the entry bytes. Version 1
// entries are the look contexts as the engine's reflection JSON, tag = look.h slot.
#pragma pack(push, 1)
struct BodyCardHeader {
    uint8_t kind;
    uint8_t version;
    uint16_t count;
    uint32_t hash;  // FNV-1a 32 of the entry bytes: unchanged card, nothing to rebuild
    uint32_t length;
    uint32_t reserved;
};
#pragma pack(pop)
static_assert(sizeof(BodyCardHeader) == 16);
constexpr uint8_t kCardVersionJson = 1;
constexpr uint16_t kMaxCardEntries = 16;
constexpr uint32_t kMaxCardBytes = 256 * 1024;  // the look JSON measured by the echo probe sets the real bound
constexpr size_t kCardEntryHeader = 4;

struct CardEntry {
    uint16_t tag;
    std::span<const uint8_t> bytes;
};
struct BodyCard {
    BodyCardHeader header;
    std::vector<CardEntry> entries;
};

template <typename T>
std::vector<uint8_t> encode(const T& message) {
    std::vector<uint8_t> out(sizeof(T));
    std::memcpy(out.data(), &message, sizeof(T));
    return out;
}

template <typename T>
std::optional<T> decodeFixed(std::span<const uint8_t> payload) {
    if (payload.size() != sizeof(T)) return std::nullopt;
    T out;
    std::memcpy(&out, payload.data(), sizeof(T));
    return out;
}

inline std::optional<GameHello> decodeHello(std::span<const uint8_t> payload) { return decodeFixed<GameHello>(payload); }

inline std::optional<Place> decodePlace(std::span<const uint8_t> payload) {
    auto place = decodeFixed<Place>(payload);
    if (!place || place->place >= kPlaces) return std::nullopt;
    return place;
}

inline std::optional<BodyState> decodeBodyState(std::span<const uint8_t> payload) {
    auto body = decodeFixed<BodyState>(payload);
    if (!body || body->kind >= kBodyKinds || !std::isfinite(body->yaw) || !std::isfinite(body->speedType)) return std::nullopt;
    for (double axis : body->position) {
        if (!std::isfinite(axis)) return std::nullopt;
    }
    return body;
}

inline std::optional<ActionEvent> decodeActionEvent(std::span<const uint8_t> payload) {
    auto event = decodeFixed<ActionEvent>(payload);
    if (!event || event->kind >= kBodyKinds || event->layer > 1) return std::nullopt;
    return event;
}

constexpr double kMaxDaySeconds = 7.0 * 24 * 3600;  // generous: the game's own day length is read at run time

inline std::optional<Clock> decodeClock(std::span<const uint8_t> payload) {
    auto clock = decodeFixed<Clock>(payload);
    if (!clock || !std::isfinite(clock->elapsedDaySeconds) || clock->elapsedDaySeconds < 0 ||
        clock->elapsedDaySeconds > kMaxDaySeconds || !std::isfinite(clock->timeScale)) {
        return std::nullopt;
    }
    return clock;
}

// The entry bytes of a card (what `hash` covers) and the whole payload around them.
inline std::vector<uint8_t> encodeCardEntries(std::span<const std::pair<uint16_t, std::string>> entries) {
    std::vector<uint8_t> out;
    for (const auto& [tag, text] : entries) {
        const auto length = static_cast<uint16_t>(text.size());
        out.insert(out.end(), reinterpret_cast<const uint8_t*>(&tag), reinterpret_cast<const uint8_t*>(&tag) + sizeof(tag));
        out.insert(out.end(), reinterpret_cast<const uint8_t*>(&length), reinterpret_cast<const uint8_t*>(&length) + sizeof(length));
        out.insert(out.end(), text.begin(), text.end());
    }
    return out;
}

inline std::vector<uint8_t> encodeBodyCard(uint8_t kind, uint16_t count, uint32_t hash, std::span<const uint8_t> entryBytes) {
    const BodyCardHeader header{kind, kCardVersionJson, count, hash, static_cast<uint32_t>(entryBytes.size()), 0};
    std::vector<uint8_t> out = encode(header);
    out.insert(out.end(), entryBytes.begin(), entryBytes.end());
    return out;
}

inline std::optional<BodyCard> decodeBodyCard(std::span<const uint8_t> payload) {
    if (payload.size() < sizeof(BodyCardHeader)) return std::nullopt;
    BodyCard card;
    std::memcpy(&card.header, payload.data(), sizeof(card.header));
    const auto rest = payload.subspan(sizeof(BodyCardHeader));
    if (card.header.kind >= kBodyKinds || card.header.version != kCardVersionJson || card.header.count > kMaxCardEntries ||
        card.header.length > kMaxCardBytes || card.header.length != rest.size()) {
        return std::nullopt;
    }
    size_t at = 0;
    for (uint16_t i = 0; i < card.header.count; ++i) {
        if (rest.size() - at < kCardEntryHeader) return std::nullopt;
        uint16_t tag = 0, length = 0;
        std::memcpy(&tag, rest.data() + at, sizeof(tag));
        std::memcpy(&length, rest.data() + at + sizeof(tag), sizeof(length));
        at += kCardEntryHeader;
        if (rest.size() - at < length) return std::nullopt;
        card.entries.push_back({tag, rest.subspan(at, length)});
        at += length;
    }
    if (at != rest.size()) return std::nullopt;
    return card;
}

// ENEMY_ROSTER (host to all, on change once settled and to joiners): this header, then `count` roster keys.
#pragma pack(push, 1)
struct RosterHeader {
    uint16_t loadSerial;  // the host's
    uint16_t count;
};
#pragma pack(pop)
static_assert(sizeof(RosterHeader) == 4);
constexpr uint16_t kMaxRosterKeys = 2048;

inline std::vector<uint8_t> encodeRoster(uint16_t loadSerial, std::span<const roster_rule::Key> keys) {
    const RosterHeader header{loadSerial, static_cast<uint16_t>(keys.size())};
    std::vector<uint8_t> out(sizeof(header) + keys.size() * sizeof(roster_rule::Key));
    std::memcpy(out.data(), &header, sizeof(header));
    if (!keys.empty()) std::memcpy(out.data() + sizeof(header), keys.data(), keys.size() * sizeof(roster_rule::Key));
    return out;
}

struct Roster {
    RosterHeader header;
    std::vector<roster_rule::Key> keys;
};

inline std::optional<Roster> decodeRoster(std::span<const uint8_t> payload) {
    if (payload.size() < sizeof(RosterHeader)) return std::nullopt;
    Roster out;
    std::memcpy(&out.header, payload.data(), sizeof(out.header));
    if (out.header.count > kMaxRosterKeys || payload.size() != sizeof(RosterHeader) + out.header.count * sizeof(roster_rule::Key)) {
        return std::nullopt;
    }
    out.keys.resize(out.header.count);
    if (out.header.count) std::memcpy(out.keys.data(), payload.data() + sizeof(RosterHeader), out.keys.size() * sizeof(roster_rule::Key));
    return out;
}

// ENEMY_STATE (host to all, 10 Hz, enemies near a guest): a RosterHeader-shaped header, then `count` states.
enum EnemyFlag : uint8_t { kEnemyDead = 1 };
#pragma pack(push, 1)
struct EnemyState {
    roster_rule::Key key;
    double position[3];
    float yaw;
    float hp;
    uint32_t actionHash[2];  // layers 0 and 1
    int32_t speedType;       // app.LocomotionSpeedTypeEnum value (humanoid enemies)
    uint8_t flags;
    uint8_t reserved[3];
};
// ENEMY_HIT (guest to host, reliable): what the guest's own Arisen or main pawn did to a mirrored enemy.
struct EnemyHit {
    roster_rule::Key key;
    uint8_t attackerKind;  // kKindArisen or kKindMainPawn
    uint8_t reserved[3];
    float damage;          // DamageInfo.Damage, as the guest's engine computed it
    float reaction;        // DamageInfo.DamageReaction
    int32_t reactionLevel; // DamageInfo.DmgReactionLv
    int32_t damageType;    // DamageInfo.DamageType
};
// ENEMY_SPAWN (host to all, reliable, answering ENEMY_WANT): this header, then the enemy's generate record as the
// engine's reflection JSON (app.GenerateManager.GenerateRequestCharacterSaveData).
struct SpawnHeader {
    roster_rule::Key key;
    uint32_t length;
};
#pragma pack(pop)
static_assert(sizeof(EnemyState) == 68);
static_assert(sizeof(EnemyHit) == 40);
static_assert(sizeof(SpawnHeader) == 24);
constexpr uint16_t kMaxStatesPerFrame = 48;
constexpr uint32_t kMaxSpawnJson = 32 * 1024;

inline std::vector<uint8_t> encodeStates(uint16_t loadSerial, std::span<const EnemyState> states) {
    const RosterHeader header{loadSerial, static_cast<uint16_t>(states.size())};
    std::vector<uint8_t> out(sizeof(header) + states.size() * sizeof(EnemyState));
    std::memcpy(out.data(), &header, sizeof(header));
    if (!states.empty()) std::memcpy(out.data() + sizeof(header), states.data(), states.size() * sizeof(EnemyState));
    return out;
}

inline std::optional<std::vector<EnemyState>> decodeStates(std::span<const uint8_t> payload) {
    if (payload.size() < sizeof(RosterHeader)) return std::nullopt;
    RosterHeader header;
    std::memcpy(&header, payload.data(), sizeof(header));
    if (header.count > kMaxStatesPerFrame || payload.size() != sizeof(header) + header.count * sizeof(EnemyState)) return std::nullopt;
    std::vector<EnemyState> out(header.count);
    if (header.count) std::memcpy(out.data(), payload.data() + sizeof(header), out.size() * sizeof(EnemyState));
    for (const EnemyState& state : out) {
        if (!std::isfinite(state.yaw) || !std::isfinite(state.hp)) return std::nullopt;
        for (double axis : state.position) {
            if (!std::isfinite(axis)) return std::nullopt;
        }
    }
    return out;
}

inline std::optional<EnemyHit> decodeEnemyHit(std::span<const uint8_t> payload) {
    auto hit = decodeFixed<EnemyHit>(payload);
    if (!hit || hit->attackerKind >= kBodyKinds || !std::isfinite(hit->damage) || !std::isfinite(hit->reaction)) return std::nullopt;
    return hit;
}

inline std::vector<uint8_t> encodeSpawn(const roster_rule::Key& key, const std::string& json) {
    const SpawnHeader header{key, static_cast<uint32_t>(json.size())};
    std::vector<uint8_t> out = encode(header);
    out.insert(out.end(), json.begin(), json.end());
    return out;
}

struct Spawn {
    roster_rule::Key key;
    std::string json;
};

inline std::optional<Spawn> decodeSpawn(std::span<const uint8_t> payload) {
    if (payload.size() < sizeof(SpawnHeader)) return std::nullopt;
    SpawnHeader header;
    std::memcpy(&header, payload.data(), sizeof(header));
    if (header.length == 0 || header.length > kMaxSpawnJson || payload.size() != sizeof(header) + header.length) return std::nullopt;
    return Spawn{header.key, std::string(reinterpret_cast<const char*>(payload.data() + sizeof(header)), header.length)};
}

// ENEMY_WANT (guest to host, reliable): the keys the guest is missing, in the ENEMY_ROSTER layout (encodeRoster).

}  // namespace wire_dd2
