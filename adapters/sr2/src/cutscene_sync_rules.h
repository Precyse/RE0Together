#pragma once
#include <cstddef>
#include <cstdint>

#include "sr2_addresses.h"

// The pure rules of the cutscene handshake (milestone 9 MC1, MC2, unit tested). The cutscene message cutscn_pkt (0x6F)
// carries three handshakes that the game keeps as one byte "a remote did it" each: ready (sub 1), skip (sub 2) and done
// (sub 3). With one remote that is the remote; with 3 or 4 players the first remote's message set the byte, so a slow
// loader ran behind, one skip press ended the cutscene for all, and the host finished before the others were done. Here a
// byte stays clear until every remote member in the game has sent its message.
namespace cutscene_sync_rules {

constexpr size_t kMaxMembers = sr2::kMaxWalkedPlayers;
constexpr int8_t kSubReady = 1;
constexpr int8_t kSubSkip = 2;
constexpr int8_t kSubDone = 3;

enum class Handshake { Ready, Skip, Done, None };

// The handshake a message belongs to. A skip counts one press for everyone in a cutscene that is skipped together (the game
// sets both bytes at once then), so it passes through.
inline Handshake handshakeOf(int8_t sub, bool skippedTogether) {
    switch (sub) {
        case kSubReady: return Handshake::Ready;
        case kSubSkip: return skippedTogether ? Handshake::None : Handshake::Skip;
        case kSubDone: return Handshake::Done;
        default: return Handshake::None;
    }
}

// Who has sent each handshake in the running cutscene.
class SenderSets {
public:
    void add(Handshake handshake, uintptr_t member) {
        Set& set = of(handshake);
        if (has(set, member) || set.count == kMaxMembers) return;
        set.members[set.count++] = member;
    }

    // True when every member of `required` is in the set (also when nobody is required: a lone player's own cutscene).
    bool complete(Handshake handshake, const uintptr_t* required, size_t requiredCount) const {
        const Set& set = of(handshake);
        for (size_t i = 0; i < requiredCount; ++i) {
            if (!has(set, required[i])) return false;
        }
        return true;
    }

    void clear() { sets_[0] = sets_[1] = sets_[2] = Set{}; }

private:
    struct Set {
        uintptr_t members[kMaxMembers] = {};
        size_t count = 0;
    };

    static bool has(const Set& set, uintptr_t member) {
        for (size_t i = 0; i < set.count; ++i) {
            if (set.members[i] == member) return true;
        }
        return false;
    }

    Set& of(Handshake handshake) { return sets_[static_cast<size_t>(handshake)]; }
    const Set& of(Handshake handshake) const { return sets_[static_cast<size_t>(handshake)]; }

    Set sets_[3];
};

}  // namespace cutscene_sync_rules
