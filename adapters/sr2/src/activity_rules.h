#pragma once
#include <cstddef>
#include <cstdint>

#include "sr2_addresses.h"

// The pure rules of milestone 7 (no game access, unit tested): which activities count every player, when a decline
// ends a start request, and which messages a spectator ignores.
namespace activity_rules {

constexpr size_t kMaxMembers = sr2::kMaxWalkedPlayers;

// Activity type ids whose scoring, goals and start position take part for every player (docs/SR2_NOTES.md "Activities").
constexpr int32_t kAllPlayersTypes[] = {4, 5, 7, 8, 0xA, 0xB, 0xD, 0x10};

inline bool isAllPlayersType(int32_t type) {
    for (const int32_t each : kAllPlayersTypes) {
        if (each == type) return true;
    }
    return false;
}

// Who has declined a start request. A decline ends the request for everyone only when every remote member has
// declined; the tally starts over when the request (its key) changes.
class DeclineTally {
public:
    // Counts `member`'s decline of the request `key`. True when every member of `required` has declined it.
    bool declinedAll(uint32_t key, uintptr_t member, const uintptr_t* required, size_t requiredCount) {
        if (key != key_) {
            key_ = key;
            count_ = 0;
        }
        if (!has(member) && count_ < kMaxMembers) declined_[count_++] = member;
        for (size_t i = 0; i < requiredCount; ++i) {
            if (!has(required[i])) return false;
        }
        return true;
    }

private:
    bool has(uintptr_t member) const {
        for (size_t i = 0; i < count_; ++i) {
            if (declined_[i] == member) return true;
        }
        return false;
    }

    uint32_t key_ = 0;
    uintptr_t declined_[kMaxMembers] = {};
    size_t count_ = 0;
};

// The two competitive diversions (cat and mouse, death tag) are 1 against 1: with more than one remote player they do
// not start.
inline bool blocksCompetitiveStart(size_t remoteHumans) { return remoteHumans > 1; }

// Demolition derby act 0x1A subtypes of the garage, the car spawn and the arena warp: only the two participants run them.
inline bool derbyFlowIgnored(uint32_t localHandle, uint32_t owner, uint32_t partner, uint8_t sub) {
    const bool flow = sub == 1 || sub == 7 || sub == 8 || sub == 9;
    return flow && localHandle != owner && localHandle != partner;
}

// A race_req answer (accept or decline) goes back to the member that invited.
inline bool racePeerGetsReply(uint8_t sub) { return sub == sr2::kRaceSubAccept || sub == sr2::kRaceSubDecline; }

}  // namespace activity_rules
