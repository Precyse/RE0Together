// Checks the follower rules: decision order, owner silence, realignment (no game). Exit 0 when every check passes.
#include <cstdio>

#include "../src/enemy_follow_rule.h"

namespace {

namespace rule = enemy_follow_rule;

int g_failures = 0;

void check(bool ok, const char* what) {
    if (ok) return;
    std::printf("FAIL: %s\n", what);
    ++g_failures;
}

rule::Decision decision(int32_t id) {
    rule::Decision d;
    d.action = {{1, id, 0, 0}};
    return d;
}

void testSequenceWraps() {
    check(rule::newer(2, 1) && !rule::newer(1, 2) && !rule::newer(5, 5), "plain order");
    check(rule::newer(1, 65535) && !rule::newer(65535, 1), "wrap-around");
}

void testNothingToTakeAtFirst() {
    rule::PendingDecision pending;
    rule::Decision out;
    check(!pending.take(out), "no decision yet");
}

void testNewestDecisionIsTakenOnce() {
    rule::PendingDecision pending;
    pending.offer(10, decision(2));
    pending.offer(11, decision(5));
    rule::Decision out;
    check(pending.take(out) && out.action.word[1] == 5, "the newest of several waiting decisions");
    check(!pending.take(out), "applied once");
}

void testLateOrRepeatedDecisionIsIgnored() {
    rule::PendingDecision pending;
    pending.offer(11, decision(5));
    rule::Decision out;
    pending.take(out);
    pending.offer(10, decision(2));
    check(!pending.take(out), "older than one already applied");
    pending.offer(11, decision(5));
    check(!pending.take(out), "a repeat");
}

void testHitSupersedesEarlierDecision() {
    rule::PendingDecision pending;
    pending.offer(10, decision(5));
    pending.supersede();
    rule::Decision out;
    check(!pending.take(out), "the hit reaction replaces a decision made before it");
    pending.offer(11, decision(7));
    check(pending.take(out) && out.action.word[1] == 7, "the owner's next decision applies");
}

void testSeedOnlyBeforeAnyDecision() {
    rule::PendingDecision fresh;
    fresh.seed(decision(3));
    rule::Decision out;
    check(fresh.take(out) && out.action.word[1] == 3, "the owner's current record stands in");

    rule::PendingDecision decided;
    decided.offer(4, decision(8));
    decided.take(out);
    decided.seed(decision(3));
    check(!decided.take(out), "a snapshot record never replaces the decision stream");

    rule::PendingDecision waiting;
    waiting.offer(4, decision(8));
    waiting.seed(decision(3));
    check(waiting.take(out) && out.action.word[1] == 8, "nor a decision still waiting");
}

void testNewEnemyInTheSlotForgetsTheOldOne() {
    rule::PendingDecision pending;
    pending.offer(20, decision(5));
    pending.forget();
    rule::Decision out;
    check(!pending.take(out), "a decision for the slot's previous enemy never reaches the new one");
    pending.seed(decision(3));
    check(pending.take(out) && out.action.word[1] == 3, "the new enemy is seeded from the owner's record again");
    pending.offer(21, decision(7));
    check(pending.take(out) && out.action.word[1] == 7, "the owner's next decisions apply");
}

void testOwnerSilenceGivesTheAiBack() {
    check(rule::thinksForOwner(true, 0), "owner heard just now");
    check(rule::thinksForOwner(true, rule::kOwnerSilentTicks - 1), "a short gap is still the owner's");
    check(!rule::thinksForOwner(true, rule::kOwnerSilentTicks), "silent owner: the local AI decides");
    check(!rule::thinksForOwner(false, 0), "not following: the local AI decides");
}

void testSeedableRecords() {
    check(rule::seedable({{1, 5, 0, 0}}) && rule::seedable({{3, 3, 0, 0}}), "an action or a lying state");
    check(!rule::seedable({{rule::kSetupState, 0, 0, 0}}), "never the setup again");
    check(!rule::seedable({{rule::kThinkState, 5, 0, 0}}), "never the think state");
}

void testActionBoundary() {
    check(!rule::atBoundary({{1, 5, 2, 1}}, {{1, 5, 0, 1}}), "a step inside the same action is no boundary");
    check(rule::atBoundary({{1, 7, 0, 0}}, {{1, 5, 3, 1}}), "a new action id");
    check(rule::atBoundary({{2, 5, 0, 0}}, {{1, 5, 3, 1}}), "a new state");
}

void testFollowerBoundary() {
    using rule::AtBoundary;
    check(rule::atOwnBoundary({{1, 5, 0, 0}}, {{1, 5, 2, 1}}) == AtBoundary::Agree, "same action: left alone");
    check(rule::atOwnBoundary({{1, 5, 0, 0}}, {{1, 8, 1, 1}}) == AtBoundary::Apply, "another action: the owner's");
    check(rule::atOwnBoundary({{1, 5, 0, 0}}, {{3, 3, 0, 0}}) == AtBoundary::OtherState,
          "another state stays with the class");
    const rule::Action start = rule::startOf({{1, 8, 1, 1}});
    check(start == rule::Action{{1, 8, 0, 1}}, "the owner's action from its first step, its parameter kept");
}

void testRealignOnlyBeyondTheGap() {
    check(!rule::realigns(rule::kRealignDistance), "at the limit: left alone");
    check(rule::realigns(rule::kRealignDistance + 1.0f), "beyond: realigned");
}

}  // namespace

int main() {
    testSequenceWraps();
    testNothingToTakeAtFirst();
    testNewestDecisionIsTakenOnce();
    testLateOrRepeatedDecisionIsIgnored();
    testHitSupersedesEarlierDecision();
    testSeedOnlyBeforeAnyDecision();
    testNewEnemyInTheSlotForgetsTheOldOne();
    testOwnerSilenceGivesTheAiBack();
    testSeedableRecords();
    testActionBoundary();
    testFollowerBoundary();
    testRealignOnlyBeyondTheGap();
    if (g_failures == 0) std::printf("enemy_follow_rule_test: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
