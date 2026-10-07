// Checks the owner decision cue (no game). Exit 0 when every check passes.
#include <cstdio>

#include "../src/enemy_action_rule.h"

namespace {

namespace rule = enemy_action_rule;

int g_failures = 0;

void check(bool ok, const char* what) {
    if (ok) return;
    std::printf("FAIL: %s\n", what);
    ++g_failures;
}

rule::Action action(int32_t state, int32_t id, int32_t a, int32_t b) { return {{state, id, a, b}}; }

void testNothingBeforeAnOwnerRecord() {
    rule::Cue cue;
    check(!cue.due(action(1, 2, 1, 0), 10000), "no owner record: nothing to apply");
}

void testLocalAiThatAgreesIsLeftAlone() {
    rule::Cue cue;
    cue.observeOwner(action(1, 2, 1, 0), 1000);
    check(!cue.due(action(1, 2, 1, 0), 1000 + rule::kGraceMs), "same record: nothing to apply");
    check(!cue.due(action(1, 5, 0, 0), 5000), "agreement settles the change for good");
}

void testSameActionInAnotherStateAgrees() {
    rule::Cue cue;
    cue.observeOwner(action(2, 5, 2, 1), 1000);
    check(!cue.due(action(1, 5, 2, 1), 1000 + rule::kGraceMs), "same action id: the local action is not restarted");
}

void testDisagreementWaitsForTheGrace() {
    rule::Cue cue;
    cue.observeOwner(action(1, 8, 0, 1), 1000);
    check(!cue.due(action(1, 2, 1, 0), 1000 + rule::kGraceMs - 1), "the local AI gets the grace to agree");
    check(cue.due(action(1, 2, 1, 0), 1000 + rule::kGraceMs), "after the grace: apply");
    check(cue.owner() == action(1, 8, 0, 1), "the owner's record is the one applied");
}

void testAnUnchangedOwnerIsAppliedOnce() {
    rule::Cue cue;
    cue.observeOwner(action(1, 8, 0, 1), 1000);
    check(cue.due(action(1, 2, 1, 0), 2000), "applied once");
    cue.observeOwner(action(1, 8, 0, 1), 2050);
    check(!cue.due(action(1, 3, 1, 0), 9000), "the same owner record never again, whatever the local AI does");
}

void testAnOwnerChangeIsAppliedAgain() {
    rule::Cue cue;
    cue.observeOwner(action(1, 8, 0, 1), 1000);
    check(cue.due(action(1, 2, 1, 0), 2000), "first change applied");
    cue.observeOwner(action(1, 3, 1, 0), 3000);
    check(cue.due(action(1, 8, 0, 1), 3000 + rule::kGraceMs), "the next change applies too");
}

void testABusyOwnerKeepsTheFirstChangeTime() {
    rule::Cue cue;
    cue.observeOwner(action(1, 8, 0, 1), 1000);
    cue.observeOwner(action(1, 9, 0, 0), 1000 + rule::kGraceMs - 10);
    check(cue.due(action(1, 2, 1, 0), 1000 + rule::kGraceMs), "the grace runs from the first pending change");
    check(cue.owner() == action(1, 9, 0, 0), "and the newest record is applied");
}

}  // namespace

int main() {
    testNothingBeforeAnOwnerRecord();
    testLocalAiThatAgreesIsLeftAlone();
    testSameActionInAnotherStateAgrees();
    testDisagreementWaitsForTheGrace();
    testAnUnchangedOwnerIsAppliedOnce();
    testAnOwnerChangeIsAppliedAgain();
    testABusyOwnerKeepsTheFirstChangeTime();
    if (g_failures == 0) std::printf("enemy_action_rule_test: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
