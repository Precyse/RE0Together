// The pure rules of the net handlers of milestone 16 wave 2 (no game): the busted wait deadlines and decisions, the race hold bits, the
// mission replay verdicts and names, the confessional choices and lock, and the session lost name. With one remote every rule gives
// the answer the shipped game gives.
#include <cmath>
#include <cstdio>
#include <limits>
#include <string_view>

#include "../src/net_handlers_rules.h"

namespace {

using net_handlers_rules::BustedWaits;
using net_handlers_rules::ConfessHolders;
using net_handlers_rules::RaceCall;
using net_handlers_rules::Verdict;
using partner_rules::Pairing;

constexpr int32_t kWrap = 0x6B49D200;
constexpr int32_t kHalf = 0x35A4E900;
constexpr int32_t kWait = 30000;
constexpr uintptr_t kSession = 0x1000;
constexpr uintptr_t kOtherSession = 0x2000;
constexpr uintptr_t kSecond = 0xB1;
constexpr uintptr_t kThird = 0xB2;
constexpr uintptr_t kFourth = 0xB3;
constexpr uintptr_t kHumanSecond = 0xA1;
constexpr uintptr_t kHumanThird = 0xA2;
constexpr uintptr_t kHumanFourth = 0xA3;
constexpr size_t kNone = partner_rules::kNone;

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_failures;
}

Pairing pairingOf(size_t remotes) {
    Pairing pairing{};
    pairing.trusted = true;
    const uintptr_t humans[] = {kHumanSecond, kHumanThird, kHumanFourth};
    const uintptr_t members[] = {kSecond, kThird, kFourth};
    for (size_t i = 0; i < remotes; ++i) pairing.items[pairing.count++] = {humans[i], members[i], static_cast<uint8_t>(i + 1), 0xD};
    return pairing;
}

void theDeadlines() {
    check(net_handlers_rules::deadlineAfter(1000) == 1000 + kWait, "a wait ends 30 s later");
    check(net_handlers_rules::deadlineAfter(kWrap - kWait) == kWrap, "the last tick before the wrap keeps the clock value");
    check(net_handlers_rules::deadlineAfter(kWrap - kWait + 1) == 1, "one tick later the deadline wraps to the start of the clock");
    check(net_handlers_rules::deadlineAfter(kWrap) == kWait, "a wait that starts at the end of the clock ends 30 s into the next lap");

    check(!net_handlers_rules::pending(-1, 1000) && !net_handlers_rules::pending(-1, 0), "a negative deadline is never pending");
    check(net_handlers_rules::pending(31000, 1000), "a deadline ahead is pending");
    check(!net_handlers_rules::pending(31000, 31000) && !net_handlers_rules::pending(31000, 31001), "the deadline itself and later: run out");
    check(net_handlers_rules::pending(1000 + kHalf - 1, 1000), "a deadline just inside half the clock ahead is pending");
    check(!net_handlers_rules::pending(1000 + kHalf, 1000), "a deadline half the clock ahead is a wrapped one: run out");
    check(!net_handlers_rules::pending(1000, 1000 + kHalf - 1), "a deadline just inside half the clock behind: run out");
    check(net_handlers_rules::pending(1000, 1000 + kHalf), "a deadline half the clock behind: the clock wrapped, it is pending");
    check(net_handlers_rules::pending(kWait - 100, kWrap - 100), "a wait across the wrap is pending before the clock wraps");
    check(net_handlers_rules::pending(kWait - 100, 5), "and after the clock wrapped");
    check(!net_handlers_rules::pending(kWait - 100, kWait), "and runs out past its deadline");
}

void theBustedWaits() {
    BustedWaits waits;
    check(waits.size() == 0 && !waits.waiting(kSession, kSecond, 0), "an empty table has nobody waiting");
    waits.record(kSession, kSecond, 31000);
    check(waits.waiting(kSession, kSecond, 1000) && !waits.waiting(kSession, kThird, 1000), "a member waits until its deadline, another does not");
    check(!waits.waiting(kSession, kSecond, 31001), "the wait runs out");
    check(!waits.waiting(kOtherSession, kSecond, 1000), "a wait of another session does not count");
    waits.record(kSession, kSecond, 61000);
    check(waits.size() == 1 && waits.waiting(kSession, kSecond, 31001), "a second bust of the same member replaces its deadline");
    waits.record(kSession, kThird, 41000);
    check(waits.size() == 2 && waits.waiting(kSession, kThird, 1000) && waits.waiting(kSession, kSecond, 1000), "each member keeps its own wait");
    waits.forget(kSecond);
    check(waits.size() == 1 && !waits.waiting(kSession, kSecond, 1000) && waits.waiting(kSession, kThird, 1000), "a leaver's wait goes, the others stay");
    waits.forget(kFourth);
    check(waits.size() == 1, "forgetting a member without a wait changes nothing");
    waits.record(kOtherSession, kSecond, 51000);
    check(waits.size() == 1 && !waits.waiting(kSession, kThird, 1000) && waits.waiting(kOtherSession, kSecond, 1000),
          "a record in a new session drops the stale entries of the old one");
    check(waits.clear() == 1 && waits.size() == 0 && !waits.waiting(kOtherSession, kSecond, 1000), "clear empties the table and says how many it held");

    BustedWaits full;
    for (uintptr_t member = 1; member <= net_handlers_rules::kMaxMembers + 3; ++member) full.record(kSession, member, 31000);
    check(full.size() == net_handlers_rules::kMaxMembers, "the table holds at most one entry per walked member");

    BustedWaits one;
    int32_t shipped = -1;
    bool sameAsShipped = true;
    const int32_t starts[] = {1000, 400000, kWrap - kWait, kWrap - kWait + 1};
    for (const int32_t start : starts) {
        shipped = net_handlers_rules::deadlineAfter(start);
        one.record(kSession, kSecond, shipped);
        for (const int32_t offset : {0, 1, kWait - 1, kWait, kWait + 1}) {
            const int32_t now = start + offset > kWrap ? start + offset - kWrap : start + offset;
            sameAsShipped = sameAsShipped && one.waiting(kSession, kSecond, now) == net_handlers_rules::pending(shipped, now);
        }
        one.clear();
        shipped = -1;
        sameAsShipped = sameAsShipped && !one.waiting(kSession, kSecond, start);
    }
    check(sameAsShipped, "one remote: the table answers as the single deadline [0xEB4490] of the shipped game, also after a clear");
}

void theBustedDecisions() {
    const auto none = [](uintptr_t) { return false; };
    const auto all = [](uintptr_t) { return true; };
    const auto onlyThird = [](uintptr_t human) { return human == kHumanThird; };
    const Pairing one = pairingOf(1);
    const Pairing two = pairingOf(2);
    const Pairing three = pairingOf(3);

    check(net_handlers_rules::hostClears(true, one, kHumanSecond, none) && !net_handlers_rules::hostClears(false, one, kHumanSecond, all),
          "one remote: the host clears exactly when it is waiting itself, as the shipped misc_19");
    check(!net_handlers_rules::hostClears(true, two, kHumanSecond, none), "two remotes: the host waits but the other remote does not: no clear");
    check(net_handlers_rules::hostClears(true, two, kHumanSecond, onlyThird), "two remotes: the host and the other remote wait: clear");
    check(!net_handlers_rules::hostClears(false, two, kHumanSecond, onlyThird), "two remotes: the host is not waiting: no clear");
    check(!net_handlers_rules::hostClears(true, three, kHumanSecond, onlyThird), "three remotes: one other remote is not waiting: no clear");
    check(net_handlers_rules::hostClears(true, three, kHumanSecond, [](uintptr_t human) { return human != kHumanSecond; }),
          "three remotes: both others wait: clear (the sender's own wait is not asked)");

    check(net_handlers_rules::localBleedsOut(true, one, kHumanSecond, none) && !net_handlers_rules::localBleedsOut(false, one, kHumanSecond, all),
          "one remote: the downed local bleeds out, as the shipped misc_19");
    check(!net_handlers_rules::localBleedsOut(true, two, kHumanSecond, none), "two remotes: the other remote can still revive: the local stays down");
    check(net_handlers_rules::localBleedsOut(true, two, kHumanSecond, onlyThird), "two remotes: the other remote is out too: the local bleeds out");
    check(!net_handlers_rules::localBleedsOut(false, two, kHumanSecond, all), "a local that is not down never bleeds out");

    check(!net_handlers_rules::hostBustClears(pairingOf(0), all), "no remote human: the host's bust does not clear");
    check(net_handlers_rules::hostBustClears(one, all) && !net_handlers_rules::hostBustClears(one, none), "one remote: clear exactly when it waits, as the shipped bust");
    check(!net_handlers_rules::hostBustClears(two, onlyThird), "two remotes: one not waiting: no clear");
    check(net_handlers_rules::hostBustClears(two, all), "two remotes waiting: clear");
    Pairing joining = pairingOf(2);
    joining.items[1].human = 0;
    check(!net_handlers_rules::hostBustClears(joining, onlyThird) && net_handlers_rules::hostBustClears(joining, [](uintptr_t human) { return human == kHumanSecond; }),
          "a remote member without a human yet is not asked");

    const net_handlers_rules::OthersCount others = net_handlers_rules::countOthers(three, kHumanSecond, onlyThird);
    check(others.matching == 1 && others.total == 2, "countOthers: 1 of the 2 remotes other than the sender");
    const net_handlers_rules::OthersCount everyone = net_handlers_rules::countOthers(three, 0, all);
    check(everyone.matching == 3 && everyone.total == 3, "countOthers without an exception counts every remote");
}

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
    constexpr int8_t kAccept = 3;
    constexpr int8_t kDecline = 0;
    check(net_handlers_rules::guestTakes(true, kSecond, kHost) && net_handlers_rules::guestTakes(false, kHost, kHost) &&
              !net_handlers_rules::guestTakes(false, kSecond, kHost),
          "the host takes misc_1a and misc_1c from anyone, a guest only from the host member");

    check(net_handlers_rules::replayVerdict(false, kHost, kHost, kDecline, false, false) == Verdict::Pass,
          "a guest takes the host's answer");
    check(net_handlers_rules::replayVerdict(false, kSecond, kHost, kAccept, false, false) == Verdict::Drop &&
              net_handlers_rules::replayVerdict(false, kSecond, kHost, kDecline, true, true) == Verdict::Drop,
          "a guest drops another guest's answer, whatever it is");
    check(net_handlers_rules::replayVerdict(true, kSecond, kHost, kAccept, true, false) == Verdict::Pass, "the host takes an accept");
    check(net_handlers_rules::replayVerdict(true, kSecond, kHost, kDecline, true, true) == Verdict::Pass, "the host takes the decline that makes everyone decline");
    check(net_handlers_rules::replayVerdict(true, kSecond, kHost, kDecline, true, false) == Verdict::Drop, "the host holds a decline while another remote has not declined");
    check(net_handlers_rules::replayVerdict(true, kSecond, kHost, kDecline, false, false) == Verdict::Pass,
          "with the wait closed the game's handler runs, which ignores the answer");
    check(net_handlers_rules::countsDecline(true, true, kDecline) && net_handlers_rules::countsDecline(true, true, 1) &&
              !net_handlers_rules::countsDecline(true, true, kAccept) && !net_handlers_rules::countsDecline(true, false, kDecline) &&
              !net_handlers_rules::countsDecline(false, true, kDecline),
          "only the host with an open wait counts a non-accept as a decline");

    check(net_handlers_rules::replayCancelsOnLeave(true, 1, true), "a remote left, the one that remains declined: cancel");
    check(!net_handlers_rules::replayCancelsOnLeave(true, 2, false), "a remote left, a remaining one has not declined: the wait goes on");
    check(!net_handlers_rules::replayCancelsOnLeave(true, 0, true), "nobody remains: the game's own leave path cancels, not this rule");
    check(!net_handlers_rules::replayCancelsOnLeave(false, 1, true), "no wait is open: nothing to cancel");
}

void theNames() {
    wchar_t out[0x20];
    const wchar_t* one[] = {L"Ana"};
    check(net_handlers_rules::joinNames(one, 1, out, 0x20) == 3 && std::wstring_view(out) == L"Ana", "one name exact");
    const wchar_t* three[] = {L"Ana", L"Bo", L"Cy"};
    check(net_handlers_rules::joinNames(three, 3, out, 0x20) == 11 && std::wstring_view(out) == L"Ana, Bo, Cy", "three names joined with a comma and a space");
    check(net_handlers_rules::joinNames(three, 0, out, 0x20) == 0 && out[0] == 0, "no names give an empty string");
    wchar_t small[6];
    check(net_handlers_rules::joinNames(three, 3, small, 6) == 5 && std::wstring_view(small) == L"Ana, " && small[5] == 0, "cut where the room ends, with a terminator");
    wchar_t tiny[1] = {L'x'};
    check(net_handlers_rules::joinNames(three, 3, tiny, 1) == 0 && tiny[0] == 0, "a buffer of one holds just the terminator");
    wchar_t none[1] = {L'x'};
    check(net_handlers_rules::joinNames(three, 3, none, 0) == 0 && none[0] == L'x', "no room writes nothing");
    const wchar_t* empty[] = {L"", L"Bo"};
    check(net_handlers_rules::joinNames(empty, 2, out, 0x20) == 4 && std::wstring_view(out) == L", Bo", "an empty name still takes its place");
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
    theDeadlines();
    theBustedWaits();
    theBustedDecisions();
    theRaceHold();
    theReplay();
    theNames();
    theConfessional();
    theConfessHolders();
    theSessionLost();
    return g_failures == 0 ? 0 : 1;
}
