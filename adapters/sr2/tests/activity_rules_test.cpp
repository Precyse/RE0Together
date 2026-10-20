// The pure rules of milestone 7 (no game): the all-players types, the decline tally, the competitive diversion block,
// the derby spectator filter and the race answer.
#include <cstdio>

#include "../src/activity_rules.h"
#include "check.h"

namespace {

constexpr uintptr_t kSecond = 0x1000;
constexpr uintptr_t kThird = 0x2000;
constexpr uintptr_t kFourth = 0x3000;
constexpr uint32_t kRequest = 77;
constexpr uint32_t kOtherRequest = 78;

}  // namespace

int main() {
    using activity_rules::DeclineTally;

    check(activity_rules::isAllPlayersType(4) && activity_rules::isAllPlayersType(0x10) &&
              activity_rules::isAllPlayersType(0xD) && !activity_rules::isAllPlayersType(0) &&
              !activity_rules::isAllPlayersType(0xE) && !activity_rules::isAllPlayersType(-1),
          "all-players types: fraud, mayhem and zombie yes; derby, racing and none no");

    const uintptr_t both[] = {kSecond, kThird};
    {
        DeclineTally tally;
        check(!tally.declinedAll(kRequest, kSecond, both, 2), "one of two remotes declines: the start goes on");
        check(!tally.declinedAll(kRequest, kSecond, both, 2), "the same remote declining again changes nothing");
        check(tally.declinedAll(kRequest, kThird, both, 2), "the second remote declines too: the start ends");
    }
    {
        DeclineTally tally;
        check(tally.declinedAll(kRequest, kSecond, both, 1), "a single remote declining ends the start at once");
    }
    {
        DeclineTally tally;
        tally.declinedAll(kRequest, kSecond, both, 2);
        check(!tally.declinedAll(kOtherRequest, kThird, both, 2), "a new request starts the tally over");
        check(tally.declinedAll(kOtherRequest, kSecond, both, 2), "and both must decline it again");
    }
    {
        DeclineTally tally;
        const uintptr_t three[] = {kSecond, kThird, kFourth};
        tally.declinedAll(kRequest, kSecond, three, 3);
        check(!tally.declinedAll(kRequest, kThird, three, 3), "two of three remotes declined: the start goes on");
        check(tally.declinedAll(kRequest, kFourth, three, 3), "all three declined: the start ends");
        check(tally.declinedAll(kRequest, kSecond, both, 0), "no remote required: nothing to wait for");
    }

    {
        // The leave rule (replay wait, start request, zombie request): a member that leaves without answering counts as a decline.
        const uintptr_t three[] = {kSecond, kThird, kFourth};
        DeclineTally tally;
        tally.declinedAll(kRequest, kSecond, three, 3);
        const DeclineTally::LeaveOutcome partial = tally.leave(kRequest, kThird, three, 3);
        check(partial.staying == 2 && !partial.ends, "P3 leaves, P2 declined and P4 has not: the wait goes on");
        const uintptr_t left[] = {kSecond, kFourth};
        const DeclineTally::LeaveOutcome last = tally.leave(kRequest, kFourth, left, 2);
        check(last.staying == 1 && last.ends, "P4 leaves too, only P2 stays and has declined: the decline completes");
    }
    {
        DeclineTally tally;
        tally.declinedAll(kRequest, kSecond, both, 2);
        check(tally.leave(kRequest, kThird, both, 2).ends, "P2 declined, P3 leaves unanswered: the other remote left has declined, so the request ends");
    }
    {
        DeclineTally tally;
        check(!tally.leave(kRequest, kThird, both, 2).ends, "a leave with nobody declined only counts");
        check(tally.declinedAll(kRequest, kSecond, both, 2), "the remote that declines afterwards ends the request");
    }
    {
        DeclineTally tally;
        const uintptr_t one[] = {kSecond};
        const DeclineTally::LeaveOutcome only = tally.leave(kRequest, kSecond, one, 1);
        check(only.staying == 0 && !only.ends, "the only remote leaving is the game's own teardown");
        const uintptr_t twice[] = {kSecond, kThird, kSecond};
        check(tally.leave(kRequest, kSecond, twice, 3).staying == 1, "a leaver listed twice is dropped both times");
        check(tally.leave(kRequest, kFourth, both, 2).staying == 2, "a leaver that is not listed drops nobody");
        check(tally.leave(kRequest, kSecond, both, 0).staying == 0, "an empty list stays empty");
    }
    {
        DeclineTally earlier;
        earlier.leave(kRequest, kThird, both, 2);
        check(!earlier.declinedAll(kOtherRequest, kSecond, both, 2), "a leaver of an earlier request does not count for a new one");
    }

    check(!activity_rules::blocksCompetitiveStart(0) && !activity_rules::blocksCompetitiveStart(1) &&
              activity_rules::blocksCompetitiveStart(2),
          "cat and mouse and death tag start with one remote, not with two");

    constexpr uint32_t kLocal = 10, kOwner = 11, kPartner = 12;
    check(activity_rules::derbyFlowIgnored(kLocal, kOwner, kPartner, 1) &&
              activity_rules::derbyFlowIgnored(kLocal, kOwner, kPartner, 9),
          "a spectator ignores the garage and arena warp subtypes");
    check(!activity_rules::derbyFlowIgnored(kLocal, kOwner, kPartner, 2) &&
              !activity_rules::derbyFlowIgnored(kLocal, kOwner, kPartner, 5),
          "and still gets the other subtypes");
    check(!activity_rules::derbyFlowIgnored(kOwner, kOwner, kPartner, 1) &&
              !activity_rules::derbyFlowIgnored(kPartner, kOwner, kPartner, 8),
          "the owner and the partner run all of them");
    check(!activity_rules::derbyFlowIgnored(kLocal, 0, kPartner, 1) && !activity_rules::derbyFlowIgnored(kLocal, kOwner, 0, 9) &&
              !activity_rules::derbyFlowIgnored(kLocal, 0, 0, 7),
          "while a pair cell is not set nobody is told apart from the participants");

    check(activity_rules::racePeerGetsReply(sr2::kRaceSubAccept) && activity_rules::racePeerGetsReply(sr2::kRaceSubDecline) &&
              !activity_rules::racePeerGetsReply(sr2::kRaceSubInvite),
          "a race accept or decline goes to the inviter, an invite does not");
    return g_failures == 0 ? 0 : 1;
}
