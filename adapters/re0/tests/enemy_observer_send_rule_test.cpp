// Checks what the simulator sends per pool slot and round (no game): living enemies every round, one reliable terminal
// entry per death, corpses once a second, one removal per vanished or replaced unit. Exit 0 when every check passes.
#include <cstdio>

#include "../src/enemy_observer_send_rule.h"
#include "../src/pose_capture_rule.h"

namespace {

namespace rule = enemy_observer_send_rule;
using rule::Action;

constexpr uint8_t kSlot = 4;
constexpr uint32_t kEnemy = 0x1000;
int g_failures = 0;

void check(bool ok, const char* what) {
    if (ok) return;
    std::printf("FAIL: %s\n", what);
    ++g_failures;
}

Action nextRound(rule::SendPlanner& planner, bool present, bool alive, uint32_t identity = kEnemy) {
    planner.startRound();
    return planner.next(kSlot, present, alive, identity);
}

void testLivingEnemyIsSentEveryRound() {
    rule::SendPlanner planner;
    for (int i = 0; i < 5; ++i) check(nextRound(planner, true, true) == Action::Pose, "a living enemy is posed every round");
    check(nextRound(planner, false, false, 0) == Action::Removed, "when it vanishes, one removal");
    check(nextRound(planner, false, false, 0) == Action::Skip, "and then nothing");
}

void testDeathIsSentOnceReliablyThenAsCorpse() {
    rule::SendPlanner planner;
    check(nextRound(planner, true, true) == Action::Pose, "alive");
    check(nextRound(planner, true, false) == Action::Terminal, "the death is one reliable terminal entry");
    uint32_t corpses = 0;
    uint32_t terminals = 0;
    for (uint32_t i = 0; i < 2 * rule::kCorpseEveryRounds; ++i) {
        const Action action = nextRound(planner, true, false);
        corpses += action == Action::Corpse ? 1 : 0;
        terminals += action == Action::Terminal ? 1 : 0;
    }
    check(terminals == 0, "no second terminal entry");
    check(corpses == 2, "a corpse is posed once a second");
}

void testCorpsesAreStaggeredBySlot() {
    rule::SendPlanner planner;
    uint32_t firstRoundOfSlot0 = 0;
    uint32_t firstRoundOfSlot1 = 0;
    planner.next(0, true, false, kEnemy);  // both die in the same round
    planner.next(1, true, false, kEnemy);
    for (uint32_t i = 1; i <= rule::kCorpseEveryRounds && (!firstRoundOfSlot0 || !firstRoundOfSlot1); ++i) {
        planner.startRound();
        if (!firstRoundOfSlot0 && planner.next(0, true, false, kEnemy) == Action::Corpse) firstRoundOfSlot0 = i;
        if (!firstRoundOfSlot1 && planner.next(1, true, false, kEnemy) == Action::Corpse) firstRoundOfSlot1 = i;
    }
    check(firstRoundOfSlot0 != 0 && firstRoundOfSlot1 != 0 && firstRoundOfSlot0 != firstRoundOfSlot1, "two corpses are not sent in the same round");
}

void testEnemySeenDeadIsTerminalAtOnce() {
    rule::SendPlanner planner;
    check(nextRound(planner, true, false) == Action::Terminal, "an enemy first seen dead (the peer arrives late) is announced at once");
}

void testReplacedEnemyIsRemovedFirst() {
    rule::SendPlanner planner;
    check(nextRound(planner, true, true, kEnemy) == Action::Pose, "first enemy");
    check(nextRound(planner, true, true, kEnemy + 1) == Action::Removed, "another unit in the slot: the old one is removed first");
    check(nextRound(planner, true, true, kEnemy + 1) == Action::Pose, "then the new one is posed");
}

void testNewEnemyAfterDeathStartsOver() {
    rule::SendPlanner planner;
    nextRound(planner, true, true);
    check(nextRound(planner, true, false) == Action::Terminal, "died");
    check(nextRound(planner, true, true, kEnemy + 1) == Action::Removed, "the corpse is replaced");
    check(nextRound(planner, true, true, kEnemy + 1) == Action::Pose, "the replacement is alive");
    check(nextRound(planner, true, false, kEnemy + 1) == Action::Terminal, "and its own death is announced again");
}

void testClearForgetsEverything() {
    rule::SendPlanner planner;
    nextRound(planner, true, true);
    planner.clear();
    check(nextRound(planner, false, false, 0) == Action::Skip, "after clear a vanished enemy is no removal");
    check(nextRound(planner, true, true) == Action::Pose, "and the slot starts over");
}

void testPoseRateIs20Hz() {
    constexpr uint32_t kFrameMs = 17;
    pose_capture_rule::SampleTimer timer{rule::kPoseIntervalMs};
    uint32_t rounds = 0;
    for (uint32_t now = 1000; now < 11000; now += kFrameMs) rounds += timer.due(now) ? 1 : 0;
    check(rounds >= 195 && rounds <= 205, "20 rounds a second at 60 frames a second");
    pose_capture_rule::SampleTimer slow{rule::kPoseIntervalMs};
    rounds = 0;
    for (uint32_t now = 1000; now < 11000; now += 33) rounds += slow.due(now) ? 1 : 0;
    check(rounds >= 140 && rounds <= 205, "at 30 frames a second a round is at most one per frame");
    pose_capture_rule::SampleTimer puppet;
    rounds = 0;
    for (uint32_t now = 1000; now < 11000; now += kFrameMs) rounds += puppet.due(now) ? 1 : 0;
    check(rounds >= 290 && rounds <= 310, "the pose puppet's timer keeps its 30 Hz default");
}

}  // namespace

int main() {
    testLivingEnemyIsSentEveryRound();
    testDeathIsSentOnceReliablyThenAsCorpse();
    testCorpsesAreStaggeredBySlot();
    testEnemySeenDeadIsTerminalAtOnce();
    testReplacedEnemyIsRemovedFirst();
    testNewEnemyAfterDeathStartsOver();
    testClearForgetsEverything();
    testPoseRateIs20Hz();
    if (g_failures == 0) std::printf("enemy_observer_send_rule_test: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
