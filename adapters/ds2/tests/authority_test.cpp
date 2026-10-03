// authority: claim, release, decline and stop between several tables, stale epochs, owner bounce, the tie rule (no
// game), and the reject counters the outcomes feed.
#include <cstdio>
#include <vector>

#include "../src/authority.h"
#include "../src/reject_counters.h"

namespace {

using authority::AuthorityTable;
using authority::AuthMessage;
using authority::Outcome;
using authority_wire::kMsgAuthClaim;
using authority_wire::kMsgAuthDecline;
using authority_wire::kMsgAuthStop;
using authority_wire::kNoOwner;

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_failures;
}

constexpr uint64_t kVehicle = 0x20000000042;
constexpr uint8_t kHost = 0;

// Three machines, slot 0 the host. `send` delivers a message from one slot to every other table.
struct Session {
    AuthorityTable table[3];
    Session() {
        for (uint8_t slot = 0; slot < 3; ++slot) table[slot].setSession(slot, kHost);
    }
    void send(uint16_t type, uint8_t from, const AuthMessage& message) {
        for (uint8_t slot = 0; slot < 3; ++slot) {
            if (slot != from) table[slot].onMessage(type, from, message);
        }
    }
};

void claimAndRelease() {
    Session s;
    const auto claim = s.table[1].claim(kVehicle);
    check(claim && claim->epoch == 1 && claim->owner == 1, "the first claim is epoch 1 for the claimant");
    check(s.table[1].isOwner(kVehicle), "the claimant owns it at once");
    s.send(kMsgAuthClaim, 1, *claim);
    check(s.table[0].ownerOf(kVehicle) == 1 && s.table[2].ownerOf(kVehicle) == 1, "everyone learns the owner");
    check(!s.table[2].claim(kVehicle), "a third machine cannot take an owned object");
    check(!s.table[0].isOwner(kVehicle), "a bystander is not the owner");

    const auto stop = s.table[1].release(kVehicle);
    check(stop && stop->epoch == 1, "a release names the epoch it ends");
    s.send(kMsgAuthStop, 1, *stop);
    check(s.table[2].ownerOf(kVehicle) == kNoOwner && s.table[2].epochOf(kVehicle) == 2, "a release leaves it unowned one epoch up");
    const auto again = s.table[2].claim(kVehicle);
    check(again && again->epoch == 3, "the next claim is one epoch past the release");
    check(!s.table[1].release(kVehicle), "only the owner can release");
}

void staleAndDuplicate() {
    Session s;
    const AuthMessage first = *s.table[1].claim(kVehicle);
    s.send(kMsgAuthClaim, 1, first);
    check(s.table[0].onMessage(kMsgAuthClaim, 1, first) == Outcome::Duplicate, "the same claim twice is a duplicate");

    const AuthMessage stop = *s.table[1].release(kVehicle);
    s.send(kMsgAuthStop, 1, stop);
    const AuthMessage second = *s.table[2].claim(kVehicle);
    s.send(kMsgAuthClaim, 2, second);
    check(s.table[0].onMessage(kMsgAuthClaim, 1, first) == Outcome::StaleEpoch, "an older claim arriving late is dropped");
    check(s.table[0].onMessage(kMsgAuthStop, 1, stop) == Outcome::StaleEpoch, "a stop of an old epoch is dropped");
    check(s.table[0].ownerOf(kVehicle) == 2, "stale messages do not change the owner");
    check(s.table[0].onMessage(kMsgAuthStop, 2, {0x99, 1, 2, {}}) == Outcome::UnknownObject, "a stop for an unknown object is refused");
}

void tieGoesToTheLowerSlot() {
    Session s;
    const AuthMessage fromOne = *s.table[1].claim(kVehicle);
    const AuthMessage fromTwo = *s.table[2].claim(kVehicle);
    s.table[0].onMessage(kMsgAuthClaim, 1, fromOne);
    s.table[0].onMessage(kMsgAuthClaim, 2, fromTwo);
    s.table[1].onMessage(kMsgAuthClaim, 2, fromTwo);
    s.table[2].onMessage(kMsgAuthClaim, 1, fromOne);
    check(s.table[0].ownerOf(kVehicle) == 1 && s.table[1].ownerOf(kVehicle) == 1 && s.table[2].ownerOf(kVehicle) == 1,
          "two simultaneous claims end with the lower slot everywhere, in any arrival order");
    check(!s.table[2].isOwner(kVehicle) && s.table[1].isOwner(kVehicle), "the loser stops owning it");
}

void declineBarsTheOwner() {
    Session s;
    const AuthMessage assigned = *s.table[0].assign(kVehicle, 2);
    s.send(kMsgAuthClaim, 0, assigned);
    check(s.table[2].isOwner(kVehicle), "the host's assignment makes the slot the owner");
    check(!s.table[1].assign(kVehicle, 0), "only the host assigns for others");

    const AuthMessage decline = *s.table[2].decline(kVehicle);
    s.send(kMsgAuthDecline, 2, decline);
    check(s.table[0].ownerOf(kVehicle) == kNoOwner && s.table[0].epochOf(kVehicle) == 2, "a decline leaves it unowned one epoch up");
    check(!s.table[2].claim(kVehicle), "the slot that declined cannot claim it again");
    check(s.table[0].onMessage(kMsgAuthClaim, 0, {kVehicle, 5, 2, {}}) == Outcome::InvalidOwner, "an assignment to a barred slot is refused");
    const uint8_t candidates[] = {2, 1};
    check(s.table[0].pickOwner(kVehicle, candidates) == 1, "the host skips the barred slot when it picks");
    check(s.table[0].pickOwner(0x77, candidates) == 2, "an object nobody declined takes the first candidate");

    const AuthMessage next = *s.table[0].assign(kVehicle, 1);
    s.send(kMsgAuthClaim, 0, next);
    const AuthMessage release = *s.table[1].release(kVehicle);
    s.send(kMsgAuthStop, 1, release);
    check(s.table[2].claim(kVehicle).has_value(), "a voluntary release clears the bars");
}

void wrongSender() {
    Session s;
    const AuthMessage claim = *s.table[1].claim(kVehicle);
    s.send(kMsgAuthClaim, 1, claim);
    check(s.table[0].onMessage(kMsgAuthClaim, 2, {kVehicle, 9, 1, {}}) == Outcome::WrongSender, "a claim for another slot from a guest is refused");
    check(s.table[0].onMessage(kMsgAuthStop, 2, claim) == Outcome::WrongSender, "a stop from a guest that is not the owner is refused");
    check(s.table[0].onMessage(kMsgAuthDecline, 2, claim) == Outcome::WrongSender, "a decline sent for someone else is refused");
    check(s.table[2].onMessage(kMsgAuthStop, 0, claim) == Outcome::Applied, "the host can stop the owner");
    check(s.table[2].ownerOf(kVehicle) == kNoOwner, "after the host's stop it is unowned");
    check(s.table[0].onMessage(0x01FF, 1, claim) == Outcome::Malformed, "an unknown type is malformed");
}

void hostStopAndLeavingSlots() {
    Session s;
    const AuthMessage claim = *s.table[1].claim(kVehicle);
    s.send(kMsgAuthClaim, 1, claim);
    const AuthMessage stop = *s.table[0].stop(kVehicle);
    s.send(kMsgAuthStop, 0, stop);
    check(s.table[1].ownerOf(kVehicle) == kNoOwner && !s.table[1].isOwner(kVehicle), "the stopped owner learns it must stop");
    check(!s.table[1].stop(kVehicle), "a guest cannot stop an owner");

    s.send(kMsgAuthClaim, 2, *s.table[2].claim(kVehicle));
    for (AuthorityTable& table : s.table) table.dropSlot(2);
    check(s.table[0].ownerOf(kVehicle) == kNoOwner && s.table[0].epochOf(kVehicle) == 4, "a leaving slot's objects become unowned");
}

void changeHandlerAndAnnounce() {
    Session s;
    int calls = 0;
    uint8_t lastOwner = kNoOwner;
    s.table[2].setChangeHandler([&](const authority::Record&, const authority::Record& after) {
        ++calls;
        lastOwner = after.owner;
    });
    s.send(kMsgAuthClaim, 1, *s.table[1].claim(kVehicle));
    check(calls == 1 && lastOwner == 1, "the change handler hears the new owner");
    s.table[2].onMessage(kMsgAuthClaim, 1, {kVehicle, 1, 1, {}});
    check(calls == 1, "a duplicate does not call the handler");
    s.table[1].claim(0x55);
    const std::vector<AuthMessage> claims = s.table[1].claimsOf(1);
    check(claims.size() == 2, "claimsOf lists everything a slot owns, for a resend");
}

void countersFollowOutcomes() {
    reject_counters::reset();
    using reject_counters::Reason;
    check(!authority::rejectReason(Outcome::Applied) && !authority::rejectReason(Outcome::Duplicate), "applied and duplicate are not rejects");
    check(authority::rejectReason(Outcome::StaleEpoch) == Reason::StaleEpoch, "a stale epoch counts as stale");
    check(authority::rejectReason(Outcome::WrongSender) == Reason::WrongSender, "a wrong sender counts as wrong sender");
    reject_counters::count(kMsgAuthClaim, Reason::StaleEpoch, 2);
    reject_counters::count(kMsgAuthClaim, Reason::StaleEpoch);
    check(reject_counters::total(kMsgAuthClaim, Reason::StaleEpoch) == 3, "counts add up per type and reason");
    check(reject_counters::total(kMsgAuthStop, Reason::StaleEpoch) == 0, "another type is counted separately");
    const TimeUs t = 100'000'000;
    const std::string line = reject_counters::summaryIfDue(t);
    check(line == "rejects: 0x0111 stale_epoch=3", "the summary names the type and the reason");
    check(reject_counters::summaryIfDue(t + reject_counters::kLogIntervalUs).empty(), "nothing is logged when nothing changed");
    reject_counters::count(kMsgAuthStop, Reason::WrongSender);
    check(reject_counters::summaryIfDue(t + 1).empty(), "a new count waits for the log interval");
    check(!reject_counters::summaryIfDue(t + 2 * reject_counters::kLogIntervalUs).empty(), "then it is logged");
}

}  // namespace

int main() {
    claimAndRelease();
    staleAndDuplicate();
    tieGoesToTheLowerSlot();
    declineBarsTheOwner();
    wrongSender();
    hostStopAndLeavingSlots();
    changeHandlerAndAnnounce();
    countersFollowOutcomes();
    std::printf(g_failures ? "%d FAILED\n" : "all passed\n", g_failures);
    return g_failures ? 1 : 0;
}
