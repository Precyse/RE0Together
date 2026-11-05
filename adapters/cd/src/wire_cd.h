#pragma once
// Crimson Desert wire messages, adapter to adapter through the launchers (0x0300-0x03FF, cd design adapter.md 11;
// docs/CONTRACT.md lists them). Integers little-endian, structs packed; every decoder checks the exact size, finite
// floats and ranges, and a refused payload is counted by the caller.
#include <cmath>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <vector>

#include "equip_rule.h"

namespace wire_cd {

constexpr uint16_t kProto = 1;

enum Type : uint16_t {
    kHello = 0x0300,
    kWorld = 0x0301,
    kMarker = 0x0302,
    kCounters = 0x0303,
    kCharTemplate = 0x0310,
    kCharMove = 0x0311,
    kCharAction = 0x0312,
    kCharRotate = 0x0313,
    kCharEquip = 0x0315,
    kCharGone = 0x0318,
    kFieldTime = 0x0370,
    kStageBranch = 0x0390,
};

constexpr size_t kHashBytes = 32;
constexpr uint8_t kGateStates = 4;  // world_gate_rule::State values 0..3

enum WorldFlag : uint8_t { kPaused = 1, kDead = 2, kInCutscene = 4, kInBossStage = 8 };

#pragma pack(push, 1)
struct Hello {
    uint16_t proto;
    uint32_t exeTimeDateStamp;
    uint8_t adapterHash[kHashBytes];
    uint32_t siteSetMask;  // bit per installed hook set (SiteSet)
    uint8_t catalogueOk;
    uint8_t role;          // 0 host, 1 guest
};
struct World {
    uint16_t loadSerial;
    uint8_t gateState;
    uint8_t flags;
    uint32_t fieldKey;  // 0 = not known yet
    float position[3];
    uint32_t ownPlayerKey;
};
struct Marker {
    uint32_t id;
    uint64_t localClockMs;
    uint64_t qpc;
};
struct Counter {
    uint16_t id;
    uint32_t value;
};
// CHAR_TEMPLATE: this header, then `userSize` bytes of the user packet and `bodySize` bytes of the body packet (the
// sender's own ActorLoadDataAck payloads, untouched; the receiver rewrites them, template_rule.h).
struct TemplateHeader {
    uint16_t generation;  // bumps on every new own load of the player
    uint16_t loadSerial;
    uint32_t userSize;
    uint32_t bodySize;
};
// CHAR_MOVE / CHAR_ACTION / CHAR_ROTATE: this header, then the sender's own Req payload (a Move without its history).
struct StreamHeader {
    uint16_t loadSerial;
    uint16_t generation;
    uint32_t seq;
};
// CHAR_EQUIP: this header, then `count` entries: every slot changed since the template of `generation` (equip_rule.h).
struct EquipHeader {
    uint16_t generation;
    uint32_t serial;  // the sender's newest equipment serial
    uint8_t count;
};
struct EquipEntry {
    uint16_t slot;
    uint32_t item;      // 0 = empty
    uint8_t hadAtLoad;  // 0 or 1
};
// FIELD_TIME (host to all, every 5 s): the host's time of day as raw units at the moment it was sent.
struct FieldTimeMessage {
    uint64_t raw;
    uint16_t scale;
    uint8_t flags;  // FieldTimeFlag
};
enum FieldTimeFlag : uint8_t { kFixedTime = 1, kSleeping = 2 };
// STAGE_BRANCH (host to all): one story log entry (story_rule.h); the whole log again to every newly matching peer.
struct StageBranchMessage {
    uint32_t logId;
    uint32_t seq;           // dense, from 1
    uint32_t sequencerKey;
    uint32_t fromBranch;    // the branch the host's instance left; UINT32_MAX when the host did not know it
    uint32_t branch;
    uint16_t flags;         // the host's StageBranchStateReq flags
};
struct Gone {
    uint8_t reason;  // 0 leaving, 1 loading
};
#pragma pack(pop)
static_assert(sizeof(TemplateHeader) == 12);
static_assert(sizeof(StreamHeader) == 8);
static_assert(sizeof(Gone) == 1);
static_assert(sizeof(FieldTimeMessage) == 11);
static_assert(sizeof(StageBranchMessage) == 22);
static_assert(sizeof(EquipHeader) == 7);
static_assert(sizeof(EquipEntry) == 7);
static_assert(sizeof(Hello) == 44);
static_assert(sizeof(World) == 24);
static_assert(sizeof(Marker) == 20);
static_assert(sizeof(Counter) == 6);

enum SiteSet : uint32_t { kSetFunnels = 1, kSetTick = 2, kSetEquipment = 4, kSetMinimap = 8, kSetContainment = 16, kSetStory = 32 };

// Counter ids in CD_COUNTERS.
enum CounterId : uint16_t {
    kC2SPackets = 1,
    kS2CPackets = 2,
    kMalformed = 3,
    kEventsDropped = 4,
    kObserverFaults = 5,
    kCaptureDropped = 6,
    kWireRefused = 7,
    kTicks = 8,
    kTemplatesSent = 9,
    kTemplatesApplied = 10,
    kMovesSent = 11,
    kMovesApplied = 12,
    kActionsSent = 13,
    kActionsApplied = 14,
    kActionsHeld = 15,
    kInjected = 16,
    kInjectDropped = 17,
    kEquipSent = 18,
    kEquipApplied = 19,
    kMinimapIcons = 20,
    kPinsCreated = 21,
    kContainedUpdates = 22,
    kFieldTimesPosted = 23,
    kFieldTimesConfirmed = 24,
    kStoryLogged = 25,
    kStoryReceived = 26,
    kStoryWouldApply = 27,
    kStoryPosted = 28,
    kStoryApplied = 29,
    kSaveHolds = 30,
    kSavesHeld = 31,
};
constexpr size_t kMaxCounters = 64;

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

inline std::optional<Hello> decodeHello(std::span<const uint8_t> payload) {
    auto hello = decodeFixed<Hello>(payload);
    if (!hello || hello->role > 1 || hello->catalogueOk > 1) return std::nullopt;
    return hello;
}

inline std::optional<World> decodeWorld(std::span<const uint8_t> payload) {
    auto world = decodeFixed<World>(payload);
    if (!world || world->gateState >= kGateStates) return std::nullopt;
    for (float axis : world->position) {
        if (!std::isfinite(axis)) return std::nullopt;
    }
    return world;
}

constexpr uint16_t kMaxFieldTimeScale = 1000;

inline std::optional<FieldTimeMessage> decodeFieldTime(std::span<const uint8_t> payload) {
    auto message = decodeFixed<FieldTimeMessage>(payload);
    if (!message || message->scale == 0 || message->scale > kMaxFieldTimeScale || message->flags > (kFixedTime | kSleeping)) {
        return std::nullopt;
    }
    return message;
}

constexpr uint32_t kNoBranch = UINT32_MAX;

inline std::optional<StageBranchMessage> decodeStageBranch(std::span<const uint8_t> payload) {
    auto message = decodeFixed<StageBranchMessage>(payload);
    if (!message || message->logId == 0 || message->seq == 0 || message->branch == kNoBranch) return std::nullopt;
    return message;
}

inline std::optional<Marker> decodeMarker(std::span<const uint8_t> payload) { return decodeFixed<Marker>(payload); }

inline std::vector<uint8_t> encodeCounters(std::span<const Counter> counters) {
    const auto count = static_cast<uint16_t>(counters.size() < kMaxCounters ? counters.size() : kMaxCounters);
    std::vector<uint8_t> out(sizeof(uint16_t) + count * sizeof(Counter));
    std::memcpy(out.data(), &count, sizeof(count));
    std::memcpy(out.data() + sizeof(count), counters.data(), count * sizeof(Counter));
    return out;
}

constexpr uint32_t kMaxLoadBytes = 0x7FC0;

struct Template {
    TemplateHeader header;
    std::span<const uint8_t> user, body;
};

inline std::vector<uint8_t> encodeTemplate(const TemplateHeader& header, std::span<const uint8_t> user, std::span<const uint8_t> body) {
    std::vector<uint8_t> out = encode(header);
    out.insert(out.end(), user.begin(), user.end());
    out.insert(out.end(), body.begin(), body.end());
    return out;
}

inline std::optional<Template> decodeTemplate(std::span<const uint8_t> payload) {
    if (payload.size() < sizeof(TemplateHeader)) return std::nullopt;
    TemplateHeader header;
    std::memcpy(&header, payload.data(), sizeof(header));
    if (header.userSize > kMaxLoadBytes || header.bodySize > kMaxLoadBytes ||
        payload.size() != sizeof(header) + header.userSize + header.bodySize) {
        return std::nullopt;
    }
    const auto rest = payload.subspan(sizeof(header));
    return Template{header, rest.first(header.userSize), rest.subspan(header.userSize)};
}

struct Stream {
    StreamHeader header;
    std::span<const uint8_t> body;
};

inline std::vector<uint8_t> encodeStream(const StreamHeader& header, std::span<const uint8_t> body) {
    std::vector<uint8_t> out = encode(header);
    out.insert(out.end(), body.begin(), body.end());
    return out;
}

// A stream message whose body must be exactly `bodySize` bytes.
inline std::optional<Stream> decodeStream(std::span<const uint8_t> payload, size_t bodySize) {
    if (payload.size() != sizeof(StreamHeader) + bodySize) return std::nullopt;
    StreamHeader header;
    std::memcpy(&header, payload.data(), sizeof(header));
    return Stream{header, payload.subspan(sizeof(header))};
}

constexpr uint8_t kMaxEquipEntries = equip_rule::kMaxSlot + 1;

struct Equip {
    EquipHeader header;
    std::vector<EquipEntry> entries;
};

inline std::vector<uint8_t> encodeEquip(uint16_t generation, uint32_t serial, std::span<const EquipEntry> entries) {
    const EquipHeader header{generation, serial, static_cast<uint8_t>(entries.size())};
    std::vector<uint8_t> out = encode(header);
    const auto* bytes = reinterpret_cast<const uint8_t*>(entries.data());
    out.insert(out.end(), bytes, bytes + entries.size() * sizeof(EquipEntry));
    return out;
}

inline std::optional<Equip> decodeEquip(std::span<const uint8_t> payload) {
    if (payload.size() < sizeof(EquipHeader)) return std::nullopt;
    Equip out;
    std::memcpy(&out.header, payload.data(), sizeof(out.header));
    if (out.header.count > kMaxEquipEntries || payload.size() != sizeof(EquipHeader) + out.header.count * sizeof(EquipEntry)) {
        return std::nullopt;
    }
    out.entries.resize(out.header.count);
    std::memcpy(out.entries.data(), payload.data() + sizeof(EquipHeader), out.header.count * sizeof(EquipEntry));
    for (const EquipEntry& e : out.entries) {
        if (e.slot > equip_rule::kMaxSlot || e.hadAtLoad > 1) return std::nullopt;
    }
    return out;
}

inline std::optional<std::vector<Counter>> decodeCounters(std::span<const uint8_t> payload) {
    uint16_t count = 0;
    if (payload.size() < sizeof(count)) return std::nullopt;
    std::memcpy(&count, payload.data(), sizeof(count));
    if (count > kMaxCounters || payload.size() != sizeof(count) + count * sizeof(Counter)) return std::nullopt;
    std::vector<Counter> out(count);
    std::memcpy(out.data(), payload.data() + sizeof(count), count * sizeof(Counter));
    return out;
}

}  // namespace wire_cd
