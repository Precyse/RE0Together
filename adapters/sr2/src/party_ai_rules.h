#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>
#include <string>

#include "partner_rules.h"

// The pure rules of milestone 16 wave 2 (docs/SR2_NOTES.md, "Homies, followers and NPC AI (milestone 16 wave 2)"; no game access,
// unit tested): which remote takes a leader's extra follower, how many players the team odds count, and the trace texts.
namespace party_ai_rules {

constexpr size_t kMinRemotesForShare = 2;        // below this every thunk does what the replaced call did
constexpr uint32_t kFirstSharedFollower = 2;     // the leader's second follower is the first one earmarked for a remote
constexpr int8_t kAnyTeam = -1;                  // a spawn record with this team passes for every player
constexpr unsigned kTeamNibbleShift = 4;         // the team is the low nibble of the record byte, signed
constexpr size_t kMaxPlayerHumans = partner_rules::kMaxWalked + 1;  // the remotes and the local player
constexpr size_t kSummaryBufferBytes = 384;

// Player humans in a fixed list, so the odds (called for every damage roll) allocate nothing.
struct HumanList {
    uintptr_t items[kMaxPlayerHumans];
    size_t count = 0;

    void add(uintptr_t human) { items[count++] = human; }
    std::span<const uintptr_t> span() const { return {items, count}; }
};

// True when a thunk does what the replaced code did: a pairing that cannot be trusted, or fewer than two remote players.
inline bool shippedPath(bool trusted, size_t remoteHumans) { return !trusted || remoteHumans < kMinRemotesForShare; }

// The remote player humans of `pairing` in ascending slot order (a joining member has none yet).
inline HumanList remoteHumansOf(const partner_rules::Pairing& pairing) {
    HumanList remotes;
    for (size_t i = 0; i < pairing.count; ++i) {
        if (pairing.items[i].human) remotes.add(pairing.items[i].human);
    }
    return remotes;
}

// The session slot of the remote that owns `human`, partner_rules::kNoSlot for any other human (and for none).
inline uint8_t slotOfHuman(const partner_rules::Pairing& pairing, uintptr_t human) {
    if (!human) return partner_rules::kNoSlot;
    for (size_t i = 0; i < pairing.count; ++i) {
        if (pairing.items[i].human == human) return pairing.items[i].slot;
    }
    return partner_rules::kNoSlot;
}

// Which of `candidates` remotes takes a leader's follower number `followerCount` (counting the new one): the extras go round them
// from the first. Below the first shared follower, or without candidates, 0.
inline size_t secondLeaderRank(uint32_t followerCount, size_t candidates) {
    if (candidates == 0 || followerCount < kFirstSharedFollower) return 0;
    return (followerCount - kFirstSharedFollower) % candidates;
}

// The team of a spawn record: the low nibble of its byte, sign extended (so 0x0F is kAnyTeam).
inline int8_t recordTeam(uint8_t raw) {
    return static_cast<int8_t>(static_cast<int8_t>(static_cast<uint8_t>(raw << kTeamNibbleShift)) >> kTeamNibbleShift);
}

// Every player human other than `head`: the local player first (when there is one), then the remotes in slot order.
inline HumanList othersOf(uintptr_t head, uintptr_t local, std::span<const uintptr_t> remotes) {
    HumanList others;
    if (local && local != head) others.add(local);
    for (const uintptr_t remote : remotes) {
        if (remote && remote != head) others.add(remote);
    }
    return others;
}

// The one other player the shipped odds count: the default partner when the head is the local player, else the local player.
inline uintptr_t shippedOther(uintptr_t head, uintptr_t local, uintptr_t partner) { return head == local ? partner : local; }

// One other player of the odds: whether it is within range of the human asked about, and its followers that are too.
struct OtherTeam {
    bool inRange;
    uint32_t nearFollowers;
};

// The head, its followers, and every other player that is near with the followers that are near too.
inline uint32_t teamSize(uint32_t headFollowers, std::span<const OtherTeam> others) {
    uint32_t size = 1 + headFollowers;
    for (const OtherTeam& other : others) {
        if (other.inRange) size += 1 + other.nearFollowers;
    }
    return size;
}

// An NPC counts as an enemy of the team when it targets the head's team, or a near other player's team, and passes the two tests.
inline bool countsAsEnemy(bool targetsHeadTeam, bool targetsNearOtherTeam, bool hostile, bool armed) {
    return (targetsHeadTeam || targetsNearOtherTeam) && hostile && armed;
}

inline int teamOdds(int enemies, int team) { return enemies - team; }

// A count of expansions and how often the answer differs from what the default partner alone would give.
struct Pair {
    uint32_t evaluated = 0;
    uint32_t other = 0;
};

// What the trace of the NPC thunks counts (one line a minute).
struct AiCounters {
    Pair unseen;
    Pair view;
    Pair range;
    Pair reactions;
    Pair spawnTeam;
    Pair damageTeam;
    Pair followerTarget;
    Pair odds;
    Pair behaviour3;  // `other` counts the targets that came from a scoped override
};

inline std::string aiSummaryLine(const AiCounters& c) {
    char text[kSummaryBufferBytes];
    snprintf(text, sizeof(text),
             "trace: npc ai: per minute: unseen %u (%u by another remote), view %u (%u), range %u (%u), reactions %u (%u), "
             "spawn team %u (%u), damage team %u (%u), follower target %u (%u), odds %u (%u), behaviour 3 target %u (scoped %u)",
             c.unseen.evaluated, c.unseen.other, c.view.evaluated, c.view.other, c.range.evaluated, c.range.other,
             c.reactions.evaluated, c.reactions.other, c.spawnTeam.evaluated, c.spawnTeam.other, c.damageTeam.evaluated,
             c.damageTeam.other, c.followerTarget.evaluated, c.followerTarget.other, c.odds.evaluated, c.odds.other,
             c.behaviour3.evaluated, c.behaviour3.other);
    return text;
}

// "1, 2" for the slots, "none" for no slot.
inline std::string slotListText(std::span<const uint8_t> slots) {
    if (slots.empty()) return "none";
    std::string text;
    for (const uint8_t slot : slots) {
        if (!text.empty()) text += ", ";
        text += std::to_string(slot);
    }
    return text;
}

}  // namespace party_ai_rules
