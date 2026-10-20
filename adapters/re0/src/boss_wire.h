#pragma once
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <type_traits>
#include <vector>

#include "enemy_lease_wire.h"
#include "enemy_policy.h"
#include "game.h"
#include "protocol.h"
#include "room_authority_wire.h"

// Wire format of the boss hand-over (partner_mode=state, docs/RE0_NOTES.md "State mode: boss hand-over"):
// BOSS_HANDOVER 0x0169, reliable, one header and, in a prepare, the members. Pure, no game access, unit tested.
namespace boss_wire {

constexpr uint16_t kMsgBossHandover = proto::kFirstGameType + 0x69;  // 0x0169

constexpr size_t kMaxMembers = 8;  // one alive bit each in the header's mask

// Prepare: the old authority froze its boss members and offers their state. Ready: the new authority has every member.
// Commit: the old authority hands the room over once it leaves it. Outcome: the simulator's alive mask. Cancel: the
// transaction ended before the hand-over.
enum class Phase : uint8_t { Prepare, Ready, Commit, Outcome, Cancel };
constexpr uint8_t kPhaseCount = 5;

enum class Reason : uint8_t { None, Timeout, Room, Sender, Member, Busy, Left };
constexpr uint8_t kReasonCount = 7;

constexpr const char* name(Phase phase) {
    switch (phase) {
        case Phase::Prepare: return "prepare";
        case Phase::Ready: return "ready";
        case Phase::Commit: return "commit";
        case Phase::Outcome: return "outcome";
        case Phase::Cancel: return "cancel";
    }
    return "?";
}

constexpr const char* name(Reason reason) {
    switch (reason) {
        case Reason::None: return "none";
        case Reason::Timeout: return "timeout";
        case Reason::Room: return "room";
        case Reason::Sender: return "sender";
        case Reason::Member: return "member";
        case Reason::Busy: return "busy";
        case Reason::Left: return "left";
    }
    return "?";
}

struct Header {
    uint32_t token;      // from the old authority: its slot in the top byte, never 0
    uint32_t roomEpoch;  // room_authority's epoch for `scene`; a mismatch is stale
    uint16_t scene;
    uint8_t phase;       // Phase
    uint8_t count;       // members behind the header (Prepare only)
    uint8_t alive;       // bit i = member i is alive (Prepare, Outcome)
    uint8_t reason;      // Reason (Cancel only)
    uint16_t pad;        // zero
};
static_assert(sizeof(Header) == 16);
static_assert(std::is_trivially_copyable_v<Header>);

// One boss member of a prepare: the enemy in pool slot `slot` as the old authority froze it.
struct Member {
    uint8_t slot;
    uint8_t classIndex;  // index into enemy_policy::kClasses, one of the boss classes
    uint16_t spawnId;
    int16_t hp;
    uint16_t pad;        // zero
    float pos[3];
    uint32_t quat;       // smallest three, pose_wire::packQuat
    int32_t record[4];   // behaviour record +0x67a4 {state, action id, a, b}
};
static_assert(sizeof(Member) == 40);
static_assert(std::is_trivially_copyable_v<Member>);

struct Message {
    Header header;
    std::vector<Member> members;
};

// True for the classes the hand-over covers (game::kBossVtables).
constexpr bool bossClass(uint8_t classIndex) {
    for (const uintptr_t vtable : game::kBossVtables) {
        if (enemy_policy::classIndexOf(vtable) == classIndex) return true;
    }
    return false;
}

constexpr uint8_t maskOf(size_t count) { return static_cast<uint8_t>((1u << count) - 1u); }

inline bool valid(const Member& member) {
    return member.slot < game::kEnemyPoolSlots && bossClass(member.classIndex) && member.pad == 0 &&
           enemy_lease_wire::finite(member.pos) && enemy_lease_wire::recordFits(member.record);
}

inline bool distinctSlots(const std::vector<Member>& members) {
    for (size_t i = 0; i < members.size(); ++i) {
        for (size_t j = i + 1; j < members.size(); ++j) {
            if (members[i].slot == members[j].slot) return false;
        }
    }
    return true;
}

// The fields every phase shares, and what each phase may carry.
inline bool valid(const Message& message) {
    const Header& header = message.header;
    if (header.token == 0 || header.roomEpoch == 0 || header.scene >= room_authority_wire::kSceneIdLimit ||
        header.phase >= kPhaseCount || header.pad != 0 || header.count != message.members.size()) {
        return false;
    }
    switch (static_cast<Phase>(header.phase)) {
        case Phase::Prepare:
            if (header.count == 0 || header.count > kMaxMembers || header.reason != 0) return false;
            if (header.alive == 0 || (header.alive & ~maskOf(header.count)) != 0 || !distinctSlots(message.members)) return false;
            for (const Member& member : message.members) {
                if (!valid(member)) return false;
            }
            return true;
        case Phase::Ready:
        case Phase::Commit:
            return header.count == 0 && header.alive == 0 && header.reason == 0;
        case Phase::Outcome:
            return header.count == 0 && header.reason == 0;
        case Phase::Cancel:
            return header.count == 0 && header.alive == 0 && header.reason != 0 && header.reason < kReasonCount;
    }
    return false;
}

inline std::vector<uint8_t> encode(const Message& message) {
    std::vector<uint8_t> bytes(sizeof(Header) + message.members.size() * sizeof(Member));
    std::memcpy(bytes.data(), &message.header, sizeof(Header));
    if (!message.members.empty()) {
        std::memcpy(bytes.data() + sizeof(Header), message.members.data(), message.members.size() * sizeof(Member));
    }
    return bytes;
}

// The payload as a message of exactly the right size whose fields are in range, or nothing.
inline std::optional<Message> decode(std::span<const uint8_t> payload) {
    if (payload.size() < sizeof(Header) || (payload.size() - sizeof(Header)) % sizeof(Member) != 0) return std::nullopt;
    Message message{};
    std::memcpy(&message.header, payload.data(), sizeof(Header));
    const size_t count = (payload.size() - sizeof(Header)) / sizeof(Member);
    if (count > kMaxMembers) return std::nullopt;
    message.members.resize(count);
    if (count > 0) std::memcpy(message.members.data(), payload.data() + sizeof(Header), count * sizeof(Member));
    if (!valid(message)) return std::nullopt;
    return message;
}

}  // namespace boss_wire
