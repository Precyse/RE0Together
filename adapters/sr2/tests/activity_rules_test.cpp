// The pure rules of milestone 7 (no game): the all-players types, the decline tally, the competitive diversion block,
// the derby spectator filter and the race answer.
#include <cstdio>

#include "../src/activity_rules.h"

namespace {

constexpr uintptr_t kSecond = 0x1000;
constexpr uintptr_t kThird = 0x2000;
constexpr uintptr_t kFourth = 0x3000;
constexpr uint32_t kRequest = 77;
constexpr uint32_t kOtherRequest = 78;

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_failures;
}

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
        DeclineTally tally;
        check(tally.declinedCount(kRequest) == 0, "a fresh tally has no decline");
        tally.declinedAll(kRequest, kSecond, both, 2);
        tally.declinedAll(kRequest, kSecond, both, 2);
        check(tally.declinedCount(kRequest) == 1 && tally.declinedCount(kOtherRequest) == 0,
              "the count is the members that declined this request, once each, and 0 for another request");
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

    check(activity_rules::racePeerGetsReply(sr2::kRaceSubAccept) && activity_rules::racePeerGetsReply(sr2::kRaceSubDecline) &&
              !activity_rules::racePeerGetsReply(sr2::kRaceSubInvite),
          "a race accept or decline goes to the inviter, an invite does not");
    return g_failures == 0 ? 0 : 1;
}
