#pragma once
#include <cstddef>
#include <cstdint>

#include "partner_rules.h"
#include "pause_mask_rules.h"

// The pure rules of the net handlers of milestone 16 wave 2 (docs/SR2_NOTES.md, "Net handlers (M16 wave 2)", unit tested, no game
// access): the race hold, the mission replay start, the confessional and the session lost name. The shipped game keeps one value
// for "the partner" in each of them; here each is kept per remote, and with one remote every rule gives the shipped answer.
namespace net_handlers_rules {

constexpr size_t kMaxMembers = partner_rules::kMaxWalked;
constexpr size_t kMinRemotesForBoundary = 2;  // below it the confessional notices are shown as the game shows them

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

// ---- Mission replay start (RP4) ----

// misc_1c: the host takes it from anyone; a guest only from the host member.
inline bool guestTakes(bool isHost, uintptr_t sender, uintptr_t hostMember) { return isHost || sender == hostMember; }

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
