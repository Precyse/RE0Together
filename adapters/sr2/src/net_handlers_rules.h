#pragma once
#include <cstddef>
#include <cstdint>

#include "partner_rules.h"
#include "pause_mask_rules.h"
#include "sr2_addresses.h"

// The pure rules of the net handlers of milestone 16 wave 2 (docs/SR2_NOTES.md, "Net handlers (M16 wave 2)", unit tested, no game
// access): the busted wait of every remote, the race hold, the mission replay answers, the confessional and the session lost name.
// The shipped game keeps one value for "the partner" in each of them; here each is kept per remote, and with one remote every
// rule gives the shipped answer.
namespace net_handlers_rules {

constexpr size_t kMaxMembers = partner_rules::kMaxWalked;
constexpr size_t kMinRemotesForBoundary = 2;  // below it the confessional notices are shown as the game shows them
constexpr wchar_t kNameSeparator[] = L", ";

// ---- Busted wait (BW1 to BW5) ----

// The game's deadline of a wait that starts at `now`: 30 s later, wrapped when it passes the end of the clock.
inline int32_t deadlineAfter(int32_t now) {
    const int32_t deadline = now + sr2::kBustedWaitMs;
    return deadline > sr2::kClockWrap ? deadline - sr2::kClockWrap : deadline;
}

// The game's test (0x8CBDB0 for the local human) for a deadline that has not run out: a negative deadline is none, a later one is
// pending within half the clock, an earlier one only when the clock has wrapped past it.
inline bool pending(int32_t deadline, int32_t now) {
    if (deadline < 0) return false;
    return deadline > now ? deadline - now < sr2::kClockHalfRange : now - deadline >= sr2::kClockHalfRange;
}

// The deadline each remote member's busted wait runs until, in the session it was recorded in.
class BustedWaits {
public:
    // Keeps `member`'s deadline. Entries of another session are stale and are dropped first.
    void record(uintptr_t session, uintptr_t member, int32_t deadline) {
        dropOtherSessions(session);
        for (size_t i = 0; i < count_; ++i) {
            if (entries_[i].member == member) {
                entries_[i].deadline = deadline;
                return;
            }
        }
        if (count_ < kMaxMembers) entries_[count_++] = {session, member, deadline};
    }

    // True when `member`'s wait in `session` has not run out at `now`.
    bool waiting(uintptr_t session, uintptr_t member, int32_t now) const {
        for (size_t i = 0; i < count_; ++i) {
            if (entries_[i].session == session && entries_[i].member == member) return pending(entries_[i].deadline, now);
        }
        return false;
    }

    void forget(uintptr_t member) {
        for (size_t i = 0; i < count_; ++i) {
            if (entries_[i].member != member) continue;
            entries_[i] = entries_[--count_];
            return;
        }
    }

    // Empties the table and says how many entries it held.
    size_t clear() {
        const size_t held = count_;
        count_ = 0;
        return held;
    }

    size_t size() const { return count_; }

private:
    struct Entry {
        uintptr_t session;
        uintptr_t member;
        int32_t deadline;
    };

    void dropOtherSessions(uintptr_t session) {
        size_t kept = 0;
        for (size_t i = 0; i < count_; ++i) {
            if (entries_[i].session == session) entries_[kept++] = entries_[i];
        }
        count_ = kept;
    }

    Entry entries_[kMaxMembers] = {};
    size_t count_ = 0;
};

// How many remote humans other than `except` satisfy `test`, and how many there are (for the trace lines).
struct OthersCount {
    size_t matching;
    size_t total;
};

template <typename Test>
OthersCount countOthers(const partner_rules::Pairing& pairing, uintptr_t except, Test test) {
    OthersCount count{0, 0};
    for (size_t i = 0; i < pairing.count; ++i) {
        const uintptr_t human = pairing.items[i].human;
        if (!human || human == except) continue;
        ++count.total;
        if (test(human)) ++count.matching;
    }
    return count;
}

inline bool hasRemoteHuman(const partner_rules::Pairing& pairing) {
    for (size_t i = 0; i < pairing.count; ++i) {
        if (pairing.items[i].human) return true;
    }
    return false;
}

// misc_19 on the host from the remote with `senderHuman`: the co-op notoriety is cleared when the local human is waiting and every
// other remote human is too; otherwise the sender's deadline is recorded.
template <typename IsWaiting>
bool hostClears(bool localWaiting, const partner_rules::Pairing& pairing, uintptr_t senderHuman, IsWaiting isWaiting) {
    return localWaiting && partner_rules::othersAllDown(pairing, senderHuman, isWaiting);
}

// misc_19 on every machine: the downed local human bleeds out when every other remote human is down or waiting too.
template <typename IsDownOrWaiting>
bool localBleedsOut(bool localDown, const partner_rules::Pairing& pairing, uintptr_t senderHuman, IsDownOrWaiting isDownOrWaiting) {
    return localDown && partner_rules::othersAllDown(pairing, senderHuman, isDownOrWaiting);
}

// The host's own bust: cleared when there is a remote human and every remote human is waiting.
template <typename IsWaiting>
bool hostBustClears(const partner_rules::Pairing& pairing, IsWaiting isWaiting) {
    return hasRemoteHuman(pairing) && partner_rules::othersAllDown(pairing, 0, isWaiting);
}

// ---- Race hold (RH1, RH2) ----

// What the game is to do with the race resource set after a change of the remote hold byte.
enum class RaceCall : uint8_t { None, Load, Unload };

// The remote hold byte (a bit per session slot) before and after an event.
struct RaceChange {
    uint8_t before;
    uint8_t after;
    RaceCall call;
};

// The remote in `slot` takes the set. It is loaded only when nobody held it, the local player included.
inline RaceChange raceAcquire(uint8_t mask, uint8_t local, uint8_t slot) {
    const uint8_t after = static_cast<uint8_t>(mask | pause_mask_rules::bitOf(slot));
    const bool load = pause_mask_rules::crossesZero(mask, after) && local == 0;
    return {mask, after, load ? RaceCall::Load : RaceCall::None};
}

// The remote in `slot` lets go (a release, or the remote leaving). Nothing happens when it did not hold the set; the set is
// unloaded only when the last holder let go and the local player does not hold it.
inline RaceChange raceRelease(uint8_t mask, uint8_t local, uint8_t slot) {
    const uint8_t after = static_cast<uint8_t>(mask & ~pause_mask_rules::bitOf(slot));
    const bool unload = pause_mask_rules::crossesZero(mask, after) && local == 0;
    return {mask, after, unload ? RaceCall::Unload : RaceCall::None};
}

// ---- Mission replay (RP1 to RP4) ----

enum class Verdict : uint8_t { Pass, Drop };

// misc_1a and misc_1c: the host takes them from anyone; a guest only from the host member.
inline bool guestTakes(bool isHost, uintptr_t sender, uintptr_t hostMember) { return isHost || sender == hostMember; }

// Only on the host, while its wait dialog is open, does an answer that is not an accept count as a decline.
inline bool countsDecline(bool isHost, bool waitOpen, int8_t answer) {
    return isHost && waitOpen && answer != sr2::kReplayAnswerAccept;
}

// misc_1b: whether the game's handler runs. `declinedAll` is the tally of the declines, asked only when `countsDecline`.
inline Verdict replayVerdict(bool isHost, uintptr_t sender, uintptr_t hostMember, int8_t answer, bool waitOpen, bool declinedAll) {
    if (!guestTakes(isHost, sender, hostMember)) return Verdict::Drop;
    if (!countsDecline(isHost, waitOpen, answer)) return Verdict::Pass;
    return declinedAll ? Verdict::Pass : Verdict::Drop;
}

// A remote left while the wait is open (it counted as a decline): the wait is cancelled when remotes remain and all of them declined.
inline bool replayCancelsOnLeave(bool waitOpen, size_t remaining, bool allRemainingDeclined) {
    return waitOpen && remaining > 0 && allRemainingDeclined;
}

// The names joined with ", " into `out` (room for `cap` characters with the terminator), cut where the room ends. The length.
inline size_t joinNames(const wchar_t* const* names, size_t count, wchar_t* out, size_t cap) {
    if (cap == 0) return 0;
    size_t length = 0;
    const auto append = [&](const wchar_t* text) {
        for (; *text != 0 && length + 1 < cap; ++text) out[length++] = *text;
    };
    for (size_t i = 0; i < count; ++i) {
        if (i > 0) append(kNameSeparator);
        append(names[i]);
    }
    out[length] = 0;
    return length;
}

// ---- Confessional (CF1 to CF9) ----

// A remote farther than the limit (a squared distance) from the confessional. NaN is not too far, as the game's compare has it.
inline bool tooFar(float distanceSquared, double limit) { return distanceSquared > limit; }

// The index of the least cash, kNone without any. Ties go to the earlier one.
inline size_t poorestIndex(const int32_t* cash, size_t count) {
    size_t best = partner_rules::kNone;
    for (size_t i = 0; i < count; ++i) {
        if (best == partner_rules::kNone || cash[i] < cash[best]) best = i;
    }
    return best;
}

// A cash below the price (signed, as the game's jl).
inline bool shortOf(int32_t cash, int32_t price) { return cash < price; }

// A receiver of the "too far" and "can't afford" notices: always with fewer than two remote humans (nothing disagrees then), else
// only when the notice is true where it is shown.
inline bool showsNotice(size_t remoteHumans, bool failsHere) { return remoteHumans < kMinRemotesForBoundary || failsHere; }

// The remotes that are in a confessional, in the order they got in. The game keeps one lock (the object a remote is in, 0 when none);
// here it is the newest remaining holder's object. A lock the game cleared by itself (0) makes the set stale.
class ConfessHolders {
public:
    struct Holder {
        uintptr_t member;
        uintptr_t object;
    };

    // `member` got into the confessional `object`. `lock` is the game's lock now. The new lock.
    uintptr_t set(uintptr_t lock, uintptr_t member, uintptr_t object) {
        dropIfStale(lock);
        remove(member);
        if (count_ < kMaxMembers) holders_[count_++] = {member, object};
        return value();
    }

    // `member` got out (or left). The new lock.
    uintptr_t clear(uintptr_t lock, uintptr_t member) {
        dropIfStale(lock);
        remove(member);
        return value();
    }

    bool has(uintptr_t member) const {
        for (size_t i = 0; i < count_; ++i) {
            if (holders_[i].member == member) return true;
        }
        return false;
    }

    size_t size() const { return count_; }
    const Holder& at(size_t index) const { return holders_[index]; }

private:
    void dropIfStale(uintptr_t lock) {
        if (lock == 0) count_ = 0;
    }

    void remove(uintptr_t member) {
        for (size_t i = 0; i < count_; ++i) {
            if (holders_[i].member != member) continue;
            for (size_t next = i + 1; next < count_; ++next) holders_[next - 1] = holders_[next];
            --count_;
            return;
        }
    }

    uintptr_t value() const { return count_ > 0 ? holders_[count_ - 1].object : 0; }

    Holder holders_[kMaxMembers] = {};
    size_t count_ = 0;
};

// ---- Session lost name (SL1) ----

// The human the "<host> is loading a save" dialog names: on a guest the host's human (0 once it has none, which the game answers
// with its generic text), on the host the default partner.
inline uintptr_t sessionLostChoice(bool localIsHost, uintptr_t hostHuman, uintptr_t defaultHuman) {
    return localIsHost ? defaultHuman : hostHuman;
}

}  // namespace net_handlers_rules
