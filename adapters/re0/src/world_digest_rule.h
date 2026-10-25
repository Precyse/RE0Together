#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include "event_rule.h"
#include "flag_diff.h"
#include "flag_family.h"
#include "fnv1a32.h"
#include "world_digest_wire.h"

// The world digest canary's rules (partner_mode=state): how the digest is computed and when two digests that differ are
// a divergence. Pure, no game access, unit tested.
namespace world_digest_rule {

using world_digest_wire::WorldDigest;

constexpr uint32_t kDivergeMs = 3000;   // a mismatch must last this long before it is reported
constexpr uint32_t kHoldOffMs = 15000;  // after a report the scope stays quiet this long so a resync can land

constexpr uint32_t kCrcPolynomial = 0xEDB88320u;
constexpr uint32_t kCrcInit = 0xFFFFFFFFu;
constexpr size_t kByteBits = 8;
constexpr uint32_t kBitsPerWord = 32;

// CRC-32 (IEEE) of `bytes`. The one checksum helper of the digest.
constexpr uint32_t crc32(std::span<const uint8_t> bytes) {
    uint32_t crc = kCrcInit;
    for (const uint8_t byte : bytes) {
        crc ^= byte;
        for (size_t bit = 0; bit < kByteBits; ++bit) crc = (crc >> 1) ^ ((crc & 1u) ? kCrcPolynomial : 0u);
    }
    return ~crc;
}

struct FlagCrcs {
    uint32_t story;  // every family but enemy and act
    uint32_t enemy;
    uint32_t act;
};

namespace detail {

inline uint32_t crcOfWords(const flag_diff::Words& words) {
    return crc32({reinterpret_cast<const uint8_t*>(words.data()), sizeof(words)});
}

// Moves the bits of `family` from `story` to `taken`.
inline void takeFamily(flag_family::Family family, flag_diff::Words& story, flag_diff::Words& taken) {
    for (const flag_family::Range& range : flag_family::kRanges) {
        if (range.family != family) continue;
        for (uint32_t bit = range.first; bit < range.end; ++bit) {
            const uint32_t mask = 1u << bit % kBitsPerWord;
            taken[bit / kBitsPerWord] |= story[bit / kBitsPerWord] & mask;
            story[bit / kBitsPerWord] &= ~mask;
        }
    }
}

}  // namespace detail

// The CRCs of the flag bitset with the presence flags (event_rule::kPresenceFlags) cleared, split by family so a mismatch
// names the family. A bit past the named families counts with the story.
inline FlagCrcs flagCrcs(const flag_diff::Words& live) {
    flag_diff::Words story = live;
    flag_diff::Words enemy{};
    flag_diff::Words act{};
    for (const uint32_t flag : event_rule::kPresenceFlags) story[flag / kBitsPerWord] &= ~(1u << flag % kBitsPerWord);
    detail::takeFamily(flag_family::Family::Enemy, story, enemy);
    detail::takeFamily(flag_family::Family::Act, story, act);
    return {detail::crcOfWords(story), detail::crcOfWords(enemy), detail::crcOfWords(act)};
}

// One owned room of the authority table.
struct AuthorityEntry {
    uint16_t scene;
    uint8_t owner;
    uint32_t epoch;
};

// FNV-1a over (scene, owner, epoch) of every entry in scene order, so the order the table lists them in does not matter.
inline uint32_t authorityHash(std::vector<AuthorityEntry> entries) {
    std::sort(entries.begin(), entries.end(), [](const AuthorityEntry& a, const AuthorityEntry& b) { return a.scene < b.scene; });
    uint32_t hash = fnv1a32::kOffsetBasis;
    const auto mix = [&hash](uint32_t value, size_t bytes) {
        for (size_t i = 0; i < bytes; ++i) hash = (hash ^ ((value >> (i * kByteBits)) & 0xffu)) * fnv1a32::kPrime;
    };
    for (const AuthorityEntry& entry : entries) {
        mix(entry.scene, sizeof(entry.scene));
        mix(entry.owner, sizeof(entry.owner));
        mix(entry.epoch, sizeof(entry.epoch));
    }
    return hash;
}

// Floor items are not a scope: each machine's floor journal is its own catch-up list for a joining guest, so its size
// differs by design (2026-10-25 live: "diverged (floor items)" every 18 s, each resync moving the partner again).
enum class Scope : uint8_t { Flags, EnemyFlags, ActFlags, Authority };
constexpr size_t kScopeCount = 4;

constexpr const char* name(Scope scope) {
    switch (scope) {
        case Scope::Flags: return "flags";
        case Scope::EnemyFlags: return "enemy flags";
        case Scope::ActFlags: return "act flags";
        case Scope::Authority: return "authority";
    }
    return "unknown";
}

// The digests can be told apart only at the same point of the host's order: both machines hold the world, the same last
// flag commit and no proposal of either side in flight. Anything else is a transit state, not a divergence.
constexpr bool comparable(const WorldDigest& local, const WorldDigest& peer) {
    return world_digest_wire::caughtUp(local) && world_digest_wire::caughtUp(peer) && local.flagSerial == peer.flagSerial &&
           local.pendingProposals == 0 && peer.pendingProposals == 0;
}

constexpr bool equalIn(Scope scope, const WorldDigest& a, const WorldDigest& b) {
    switch (scope) {
        case Scope::Flags: return a.flagCrc == b.flagCrc;
        case Scope::EnemyFlags: return a.enemyFlagCrc == b.enemyFlagCrc;
        case Scope::ActFlags: return a.actFlagCrc == b.actFlagCrc;
        case Scope::Authority: return a.authorityHash == b.authorityHash;
    }
    return true;
}

enum class Event : uint8_t { Diverged, Converged };

struct Verdict {
    Scope scope;
    Event event;
    bool repeat;  // Diverged: the scope was reported before and has not been equal since
};

// Judges each scope on its own. A mismatch that lasts kDivergeMs gives one Diverged; the scope is then quiet for
// kHoldOffMs and judged again after (a Diverged with `repeat`). Equality after a Diverged gives one Converged. Digests that
// are not comparable end every running mismatch: only a state both machines stood in together counts. Times wrap.
class Canary {
public:
    std::vector<Verdict> compare(const WorldDigest& local, const WorldDigest& peer, uint32_t nowMs) {
        std::vector<Verdict> verdicts;
        for (size_t i = 0; i < kScopeCount; ++i) {
            const Scope scope = static_cast<Scope>(i);
            ScopeState& state = scopes_[i];
            if (!comparable(local, peer)) {
                state.mismatching = false;
            } else if (equalIn(scope, local, peer)) {
                state.mismatching = false;
                if (state.diverged) verdicts.push_back({scope, Event::Converged, false});
                state = ScopeState{};
            } else if (inHoldOff(state, nowMs)) {
                state.mismatching = false;
            } else if (!state.mismatching) {
                state.mismatching = true;
                state.sinceMs = nowMs;
            } else if (nowMs - state.sinceMs >= kDivergeMs) {
                verdicts.push_back({scope, Event::Diverged, state.diverged});
                state.mismatching = false;
                state.diverged = true;
                state.reportedMs = nowMs;
            }
        }
        return verdicts;
    }

private:
    struct ScopeState {
        bool mismatching = false;
        uint32_t sinceMs = 0;
        bool diverged = false;  // reported, not equal since
        uint32_t reportedMs = 0;
    };

    static bool inHoldOff(const ScopeState& state, uint32_t nowMs) {
        return state.diverged && nowMs - state.reportedMs < kHoldOffMs;
    }

    std::array<ScopeState, kScopeCount> scopes_{};
};

}  // namespace world_digest_rule
