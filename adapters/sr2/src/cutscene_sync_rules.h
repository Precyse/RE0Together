#pragma once
#include <cstddef>
#include <cstdint>

#include "sr2_addresses.h"

// The pure rules of the cutscene handshake (milestone 9 MC1, MC2, unit tested). The cutscene message cutscn_pkt (0x6F)
// carries three handshakes that the game keeps as one byte "a remote did it" each: ready (sub 1), skip (sub 2) and done
// (sub 3). With one remote that is the remote; with 3 or 4 players the first remote's message set the byte, so a slow
// loader ran behind, one skip press ended the cutscene for all, and the host finished before the others were done. Here a
// byte stays clear until every remote member in the game has sent its message, a remote that leaves stops counting at once
// (milestone 11 B4), and an early ready lives as long as the game's byte does.
namespace cutscene_sync_rules {

constexpr size_t kMaxMembers = sr2::kMaxWalkedPlayers;
constexpr int8_t kSubStart = 0;
constexpr int8_t kSubReady = 1;
constexpr int8_t kSubSkip = 2;
constexpr int8_t kSubDone = 3;

enum class Handshake { Ready, Skip, Done, None };

// Sub 0: the host's cutscene starts here. The handler runs the start with the game's byte "a remote is ready" saved and put
// back around it.
inline bool isStart(int8_t sub) { return sub == kSubStart; }

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

// Who has sent each handshake in the running cutscene, and whether the game's byte for it was put back because some remote
// has not sent yet (withheld).
class SenderSets {
public:
    void add(Handshake handshake, uintptr_t member) {
        Set& set = of(handshake);
        if (has(set, member) || set.count == kMaxMembers) return;
        set.members[set.count++] = member;
    }

    // True when every member of `required` is in the set (also when nobody is required: a lone player's own cutscene).
    bool complete(Handshake handshake, const uintptr_t* required, size_t requiredCount) const {
        return completeWithout(of(handshake), required, requiredCount, 0);
    }

    // MC1 put the game's byte back (true) or let it stand (false).
    void setWithheld(Handshake handshake, bool withheld) { of(handshake).withheld = withheld; }
    bool withheld(Handshake handshake) const { return of(handshake).withheld; }

    // A cutscene starts here (MC2). Skip and done always start empty. The ready set is the game's byte "a remote is ready":
    // a guest that starts the host's cutscene (the handler of sub 0, `insideRemoteStart`) saves that byte around the start
    // and puts it back, so a remote's ready that came before the start survives; a cutscene the host starts clears it.
    void started(bool insideRemoteStart) {
        sets_[static_cast<size_t>(Handshake::Skip)] = Set{};
        sets_[static_cast<size_t>(Handshake::Done)] = Set{};
        if (!insideRemoteStart) clearReady();
    }

    // The game cleared the byte "a remote is ready": the playback started (CS1) or the wait gave up (CS2).
    void clearReady() { sets_[static_cast<size_t>(Handshake::Ready)] = Set{}; }

    // The handshakes a leaver completes.
    struct Completed {
        bool ready = false;
        bool skip = false;
        bool done = false;
    };

    // A member left (N4b): it is dropped from every set, and each handshake that was withheld and is complete without it is
    // now complete, as if its last message had arrived. Nobody left to wait for counts as complete.
    Completed memberLeft(uintptr_t leaver, const uintptr_t* required, size_t requiredCount) {
        Completed completed;
        bool* flags[] = {&completed.ready, &completed.skip, &completed.done};
        for (size_t i = 0; i < sizeof(sets_) / sizeof(sets_[0]); ++i) {
            Set& set = sets_[i];
            drop(set, leaver);
            if (!set.withheld || !completeWithout(set, required, requiredCount, leaver)) continue;
            set.withheld = false;
            *flags[i] = true;
        }
        return completed;
    }

private:
    struct Set {
        uintptr_t members[kMaxMembers] = {};
        size_t count = 0;
        bool withheld = false;
    };

    static bool has(const Set& set, uintptr_t member) {
        for (size_t i = 0; i < set.count; ++i) {
            if (set.members[i] == member) return true;
        }
        return false;
    }

    static bool completeWithout(const Set& set, const uintptr_t* required, size_t requiredCount, uintptr_t except) {
        for (size_t i = 0; i < requiredCount; ++i) {
            if (required[i] != except && !has(set, required[i])) return false;
        }
        return true;
    }

    static void drop(Set& set, uintptr_t member) {
        for (size_t i = 0; i < set.count; ++i) {
            if (set.members[i] != member) continue;
            set.members[i] = set.members[--set.count];
            return;
        }
    }

    Set& of(Handshake handshake) { return sets_[static_cast<size_t>(handshake)]; }
    const Set& of(Handshake handshake) const { return sets_[static_cast<size_t>(handshake)]; }

    Set sets_[3];
};

}  // namespace cutscene_sync_rules
