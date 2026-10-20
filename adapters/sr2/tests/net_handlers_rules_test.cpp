// The pure rules of the net handlers of milestone 16 wave 2 (no game): the race hold bits, the replay start filter, the confessional
// choices and lock, and the session lost name. With one remote every rule gives the answer the shipped game gives.
#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <limits>

#include "../src/net_handlers_rules.h"
#include "check.h"

namespace {

using net_handlers_rules::ConfessHolders;
using net_handlers_rules::RaceCall;

constexpr uintptr_t kSecond = 0xB1;
constexpr uintptr_t kThird = 0xB2;
constexpr uintptr_t kFourth = 0xB3;
constexpr uintptr_t kHumanSecond = 0xA1;
constexpr uintptr_t kHumanThird = 0xA2;
constexpr size_t kNone = partner_rules::kNone;

// The shipped game's remote hold byte: set by the first hold, cleared by a release; the set is loaded and unloaded around it.
struct ShippedRace {
    uint8_t remote = 0;
    RaceCall acquire(uint8_t local) {
        if (remote != 0) return RaceCall::None;
        remote = 1;
        return local == 0 ? RaceCall::Load : RaceCall::None;
    }
    RaceCall release(uint8_t local) {
        if (remote == 0) return RaceCall::None;
        remote = 0;
        return local == 0 ? RaceCall::Unload : RaceCall::None;
    }
};

void theRaceHold() {
    for (const uint8_t local : {uint8_t{0}, uint8_t{1}}) {
        ShippedRace shipped;
        uint8_t mask = 0;
        bool same = true;
        const bool steps[] = {true, true, false, false, true, false, false};
        for (const bool acquire : steps) {
            const net_handlers_rules::RaceChange change =
                acquire ? net_handlers_rules::raceAcquire(mask, local, 1) : net_handlers_rules::raceRelease(mask, local, 1);
            const RaceCall expected = acquire ? shipped.acquire(local) : shipped.release(local);
            same = same && change.call == expected && (change.after != 0) == (shipped.remote != 0) && change.before == mask;
            mask = change.after;
        }
        check(same, local == 0 ? "one remote, the local not racing: the same loads, unloads and zero/non-zero byte as the shipped game"
                               : "one remote, the local racing: the same, no load and no unload");
    }

    net_handlers_rules::RaceChange change = net_handlers_rules::raceAcquire(0, 0, 1);
    check(change.before == 0 && change.after == 2 && change.call == RaceCall::Load, "the first remote takes bit 1: load");
    change = net_handlers_rules::raceAcquire(change.after, 0, 2);
    check(change.after == 6 && change.call == RaceCall::None, "a second remote takes bit 2: no load");
    change = net_handlers_rules::raceRelease(6, 0, 1);
    check(change.after == 4 && change.call == RaceCall::None, "two remotes: the first release keeps the set");
    change = net_handlers_rules::raceRelease(4, 0, 2);
    check(change.after == 0 && change.call == RaceCall::Unload, "the last release unloads it");
    change = net_handlers_rules::raceRelease(4, 1, 2);
    check(change.after == 0 && change.call == RaceCall::None, "the local player holding: the last remote release does not unload");
    change = net_handlers_rules::raceRelease(4, 0, 1);
    check(change.before == 4 && change.after == 4 && change.call == RaceCall::None, "a release of a remote that did not hold changes nothing");
    change = net_handlers_rules::raceRelease(0, 0, 3);
    check(change.after == 0 && change.call == RaceCall::None, "a release with nobody holding does nothing");
    change = net_handlers_rules::raceAcquire(0x80, 0, 9);
    check(change.after == 0x80 && change.call == RaceCall::None, "slot 9 shares bit 7: taking it again keeps the byte");
    change = net_handlers_rules::raceRelease(0x80, 0, 9);
    check(change.after == 0 && change.call == RaceCall::Unload, "slot 9 releases the bit slot 7 took");
    change = net_handlers_rules::raceAcquire(2, 0, 1);
    check(change.after == 2 && change.call == RaceCall::None, "taking a set again does not load it twice");
}

void theReplay() {
    constexpr uintptr_t kHost = 0xB0;
    check(net_handlers_rules::guestTakes(true, kSecond, kHost) && net_handlers_rules::guestTakes(false, kHost, kHost) &&
              !net_handlers_rules::guestTakes(false, kSecond, kHost),
          "the host takes misc_1a and misc_1c from anyone, a guest only from the host member");
}

void theConfessional() {
    constexpr double kLimit = 1600.0;
    check(net_handlers_rules::tooFar(1600.5f, kLimit) && !net_handlers_rules::tooFar(1600.0f, kLimit) && !net_handlers_rules::tooFar(10.0f, kLimit),
          "too far is strictly beyond the limit");
    check(!net_handlers_rules::tooFar(std::numeric_limits<float>::quiet_NaN(), kLimit), "an unknown distance is not too far");
    check(std::sqrt(kLimit) == 40.0, "the limit is 40 m");

    const int32_t cash[] = {500, 100, 100, 900};
    check(net_handlers_rules::poorestIndex(cash, 4) == 1, "the least cash wins, the first of equals");
    check(net_handlers_rules::poorestIndex(cash, 1) == 0 && net_handlers_rules::poorestIndex(cash, 0) == kNone, "one remote, and none");
    const int32_t debt[] = {50, -10};
    check(net_handlers_rules::poorestIndex(debt, 2) == 1, "cash is signed");
    check(net_handlers_rules::shortOf(99, 100) && !net_handlers_rules::shortOf(100, 100) && net_handlers_rules::shortOf(-5, 0) &&
              !net_handlers_rules::shortOf(0, -5),
          "short of the price is a signed less than");

    check(net_handlers_rules::showsNotice(0, false) && net_handlers_rules::showsNotice(1, false), "with fewer than two remote humans the notice is always shown");
    check(!net_handlers_rules::showsNotice(2, false) && net_handlers_rules::showsNotice(2, true) && !net_handlers_rules::showsNotice(4, false),
          "with two or more it is shown only where it is true");
}

void theConfessHolders() {
    ConfessHolders holders;
    check(holders.set(0, kSecond, 0xC1) == 0xC1 && holders.size() == 1, "one remote: getting in stores its object, as the shipped store");
    check(holders.clear(0xC1, kSecond) == 0 && holders.size() == 0, "one remote: getting out stores 0, as the shipped store");

    check(holders.set(0, kSecond, 0xC1) == 0xC1, "the first remote gets in");
    check(holders.set(0xC1, kThird, 0xC2) == 0xC2 && holders.size() == 2, "a second one gets in: the lock is the newest");
    check(holders.clear(0xC2, kThird) == 0xC1 && holders.has(kSecond) && !holders.has(kThird), "the newest gets out: the lock goes back to the other");
    check(holders.clear(0xC1, kThird) == 0xC1, "a member that was not in changes nothing");
    check(holders.clear(0xC1, kSecond) == 0 && holders.size() == 0, "the last one gets out: the lock is 0");

    holders.set(0, kSecond, 0xC1);
    holders.set(0xC1, kThird, 0xC2);
    check(holders.set(0xC2, kSecond, 0xC3) == 0xC3 && holders.size() == 2 && holders.at(1).member == kSecond,
          "a remote that gets into another confessional becomes the newest, once");
    check(holders.clear(0xC3, kSecond) == 0xC2, "and when it gets out the lock is the other's");
    check(holders.set(0, kFourth, 0xC4) == 0xC4 && holders.size() == 1 && !holders.has(kThird),
          "a lock the game reset (0) makes the set stale: it starts over");
    holders.set(0xC4, kThird, 0xC5);
    check(holders.clear(0, kThird) == 0 && holders.size() == 0, "a clear after the game's own reset leaves nobody");
    holders.set(0, kSecond, 0xC1);
    holders.set(0xC1, kThird, 0xC2);
    check(holders.clear(0xC2, kSecond) == 0xC2 && holders.size() == 1, "a leaver that held an older confessional leaves the lock on the newest");

    ConfessHolders crowd;
    uintptr_t lock = 0;
    for (uintptr_t member = 1; member <= net_handlers_rules::kMaxMembers + 3; ++member) lock = crowd.set(lock == 0 ? 1 : lock, member, member + 0x100);
    check(crowd.size() == net_handlers_rules::kMaxMembers, "the set holds at most one holder per walked member");
}

void theSessionLost() {
    constexpr uintptr_t kHostHuman = 0xA0;
    check(net_handlers_rules::sessionLostChoice(true, kHostHuman, kHumanSecond) == kHumanSecond, "the host keeps the default partner");
    check(net_handlers_rules::sessionLostChoice(false, kHostHuman, kHumanThird) == kHostHuman, "a guest names the host's human, not the default partner");
    check(net_handlers_rules::sessionLostChoice(false, 0, kHumanThird) == 0, "a guest whose host has no human gets the generic text, not another remote");
}

}  // namespace

int main() {
    theRaceHold();
    theReplay();
    theConfessional();
    theConfessHolders();
    theSessionLost();
    return g_failures == 0 ? 0 : 1;
}
