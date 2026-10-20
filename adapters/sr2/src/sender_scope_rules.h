#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "fnv1a.h"
#include "game_message.h"
#include "sr2_addresses.h"

// The pure rules of the sender scope (milestone 9 and 11, unit tested).
namespace sender_scope_rules {

constexpr uint16_t kNoNetId = sr2::kNoNetId;

// The net id a look message (player_morph 0x45, player_components 0x46) must name. A look can only describe the sender's
// own player, so it names the sender's human; the id in the payload (a stale or reissued one may name someone else) is only
// kept while the sender has no human with a net id. With 2 players the two are the same.
inline uint16_t lookNetId(uint16_t payloadId, uint16_t senderHumanId) {
    return senderHumanId != kNoNetId ? senderHumanId : payloadId;
}

// A look is applied again only when it changed (milestone 11 B2). Every look sender broadcasts to every remote on a join,
// a load, a pause exit and a mission start, so with several players each of those re-dressed every player on every machine.
constexpr size_t kNetIdBytes = sizeof(uint16_t);  // the first word of a look payload names the human; the rest is the look
constexpr size_t kMaxLookPayload = game_message::kSendLengthLimit;  // a message this long or longer is never sent
constexpr size_t kMinRemoteHumansToDrop = 2;      // with one remote the game's handler always runs
constexpr uint8_t kLookSlots = 8;                 // sender slots 0..7

enum class LookKind : uint8_t { Morph, Components };

// Whether a payload of `length` bytes can be digested: it has a look behind the net id and fits a message.
inline bool lookCacheable(size_t length) { return length > kNetIdBytes && length <= kMaxLookPayload; }

// The digest of the look: the payload after its net id word.
inline uint64_t lookDigest(const uint8_t* payload, size_t length) {
    return fnv1a::hash64(payload + kNetIdBytes, length - kNetIdBytes);
}

struct LookKey {
    uintptr_t session;
    uint8_t slot;
    LookKind kind;

    bool operator==(const LookKey&) const = default;
};

struct Look {
    uint32_t human;  // the handle of the sender's human: a rejoiner's new human is a new look
    uint64_t digest;

    bool operator==(const Look&) const = default;
};

// The last look applied for each sender slot and message.
class LookCache {
public:
    // True when `look` is what the key already has applied: the handler is not called again. Otherwise it is stored and the
    // handler runs. With fewer than two remote humans nothing is dropped, which is the 2-player game.
    bool unchanged(const LookKey& key, const Look& look, size_t remoteHumans) {
        entries_.erase(std::remove_if(entries_.begin(), entries_.end(), [&](const Entry& e) { return e.key.session != key.session; }),
                       entries_.end());
        if (remoteHumans < kMinRemoteHumansToDrop || key.slot >= kLookSlots) return false;
        for (Entry& entry : entries_) {
            if (!(entry.key == key)) continue;
            if (entry.look == look) return true;
            entry.look = look;
            return false;
        }
        entries_.push_back({key, look});
        return false;
    }

private:
    struct Entry {
        LookKey key;
        Look look;
    };
    std::vector<Entry> entries_;
};

}  // namespace sender_scope_rules
