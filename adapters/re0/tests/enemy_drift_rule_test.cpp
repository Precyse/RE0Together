// Checks the enemy drift rules: onset and clearing after the settle time, the log budget, the summary schedule and the
// per-shot ledger. Exit 0 when every check passes.
#include <cstdio>
#include <vector>

#include "../src/enemy_drift_rule.h"

namespace {

using namespace enemy_drift_rule;
namespace obs = enemy_observer_rule;
namespace wire = enemy_observer_wire;

int g_failures = 0;

void check(bool ok, const char* what) {
    if (ok) return;
    std::printf("FAIL: %s\n", what);
    ++g_failures;
}

constexpr uint32_t kFrameMs = 16;

void testTracker() {
    Tracker tracker;
    check(tracker.observe(5, 5, 0) == Event::None, "a match does nothing");
    check(tracker.observe(5, 9, 100) == Event::None, "a fresh mismatch is not drift yet");
    check(tracker.observe(5, 9, 100 + kSettleMs - 1) == Event::None, "still inside the settle time");
    check(tracker.observe(5, 9, 100 + kSettleMs) == Event::Onset, "onset once the mismatch lasted the settle time");
    check(tracker.driftMs(100 + kSettleMs) == kSettleMs, "the drift age is counted from the first mismatch");
    check(tracker.observe(5, 9, 100 + kSettleMs + 1000) == Event::None, "the same drift never fires again");
    check(tracker.observe(5, 5, 2000) == Event::Cleared, "the first match after an onset clears it");
    check(tracker.driftMs(2000) == 1900, "the cleared drift keeps its length");
    check(tracker.observe(5, 5, 2016) == Event::None, "a match after clearing does nothing");
}

void testTrackerBlip() {
    Tracker tracker;
    tracker.observe(1, 2, 0);
    check(tracker.observe(1, 1, 200) == Event::None, "a short mismatch that ends is never reported");
    check(tracker.observe(1, 2, 250) == Event::None, "the next mismatch starts its own settle time");
    check(tracker.observe(1, 2, 250 + kSettleMs - 1) == Event::None, "the blip before did not count");
    check(tracker.observe(1, 2, 250 + kSettleMs) == Event::Onset, "onset counted from the second mismatch");
    check(tracker.observe(1, 3, 250 + kSettleMs + kFrameMs) == Event::None, "a drift to another value is the same drift");
}

void testTrackerWraps() {
    Tracker tracker;
    const uint32_t start = 0xfffffff0u;
    tracker.observe(1, 2, start);
    check(tracker.observe(1, 2, start + kSettleMs) == Event::Onset, "the settle time survives the clock wrapping");
}

void testSlotTrackers() {
    SlotTrackers slot;
    slot.observe(Field::Motion, 1, 2, 0);
    slot.observe(Field::Hp, 1, 2, 0);
    check(slot.observe(Field::Hp, 1, 2, kSettleMs) == Event::Onset, "hp settles in kSettleMs");
    check(slot.observe(Field::Motion, 1, 2, kSettleMs) == Event::None, "motion has not settled yet");
    check(slot.observe(Field::Motion, 1, 2, kMotionSettleMs) == Event::Onset, "motion settles in kMotionSettleMs");
    check(settleMs(Field::ShotSkip) == kSettleMs && settleMs(Field::RecordState) == kSettleMs, "the discrete fields use the short time");
    check(kMotionSettleMs > kSettleMs, "motion is given longer than the discrete fields");
}

void testCorrectedFields() {
    check(corrected(Field::Hp) && corrected(Field::Alive) && corrected(Field::ShotSkip), "the hit-test fields are corrected");
    check(!corrected(Field::RecordState) && !corrected(Field::Motion), "record and motion are left to the stream");
}

obs::Sample sample(int16_t hp, uint8_t flags, uint32_t flags6a0c) {
    obs::Sample shown;
    shown.hp = hp;
    shown.flags = flags;
    shown.flags6a0c = flags6a0c;
    shown.record[0] = 7;
    shown.motion0 = 0x12;
    return shown;
}

void testValues() {
    const Values living = expectedOf(sample(40, wire::kFlagAlive, 0x13));
    check(living.hp == 40 && living.alive == 1, "a living sample expects its hp and alive");
    check(living.shotSkip == 0, "a living enemy is expected with the shot-skip bits clear");
    check(living.recordState == 7 && living.motion == 0x12 && living.recordKnown, "record state and motion are the sample's");
    const Values dead = expectedOf(sample(-1, wire::kFlagTerminal, 0x13));
    check(dead.alive == 0 && dead.shotSkip == 0x3, "a corpse keeps the stream's shot-skip bits");
    const Values zeroHp = expectedOf(sample(0, wire::kFlagAlive, 0));
    check(zeroHp.alive == 0, "alive needs hp above zero");

    wire::PoseEntry read{};
    read.hp = 40;
    read.flags6a0c = 0x1;
    read.record[0] = 7;
    read.motion0 = 0x12;
    const Values seen = actualOf(read);
    check(seen.hp == 40 && seen.alive == 1 && seen.shotSkip == 1, "the read enemy shows its own bits");
    check(living.of(Field::ShotSkip) != seen.of(Field::ShotSkip), "the copied skip bit is a mismatch");
    check(living.of(Field::Hp) == seen.of(Field::Hp) && living.of(Field::Motion) == seen.of(Field::Motion), "the other fields match");

    obs::Sample constructing = sample(40, wire::kFlagAlive, 0);
    constructing.record[0] = obs::kSetupState;
    check(!expectedOf(constructing).compares(Field::RecordState), "the record is not compared while the simulator's enemy sets up");
    check(expectedOf(constructing).compares(Field::Hp), "the other fields still are");
}

void testLineBudget() {
    LineBudget budget;
    uint32_t allowed = 0;
    for (uint32_t index = 0; index < kLinesPerWindow + 5; ++index) allowed += budget.allow(1000 + index);
    check(allowed == kLinesPerWindow, "no more than kLinesPerWindow lines in a window");
    check(!budget.allow(1000 + kWindowMs - 1), "the window has not ended");
    check(budget.allow(1000 + kWindowMs), "the next window opens the budget again");
    uint32_t again = 1;
    for (uint32_t index = 0; index < kLinesPerWindow + 5; ++index) again += budget.allow(1000 + kWindowMs + index);
    check(again == kLinesPerWindow, "the new window has the same limit");
}

void testSummary() {
    Summary summary;
    check(!summary.due(0) && !summary.due(kSummaryMs * 5), "nothing is reported while every counter is zero");
    summary.corrections = 2;
    check(!summary.due(100000), "the window starts at the first count");
    check(!summary.due(100000 + kSummaryMs - 1), "not due before the interval");
    check(summary.due(100000 + kSummaryMs), "due after the interval");
    const Summary taken = summary.take();
    check(taken.corrections == 2, "take hands the counters out");
    check(!summary.any() && !summary.due(200000 + kSummaryMs), "the counters are reset and nothing is due again");
    summary.onsets[static_cast<size_t>(Field::ShotSkip)] = 1;
    check(!summary.due(300000) && summary.due(300000 + kSummaryMs), "a new window starts with the next count");
}

SkipSighting none() { return {}; }

void testShots() {
    ShotLedger ledger;
    std::vector<Shot> closed;
    ledger.fired(1000, none());
    ledger.routed(1100, false);
    ledger.routed(1100, false);
    ledger.applied();
    ledger.applied();
    ledger.expire(1000 + kAnswerWindowMs - 1, closed);
    check(closed.empty(), "a shot stays open for the answer window");
    ledger.expire(1000 + kAnswerWindowMs, closed);
    check(closed.size() == 1 && closed[0].routed == 2 && closed[0].applied == 2, "a pellet volley counts every hit and answer");
    check(!closed[0].anomalous(), "all answered is normal");

    closed.clear();
    ledger.fired(5000, none());
    ledger.routed(5100, false);
    ledger.routed(5100, false);
    ledger.applied();
    ledger.expire(5000 + kAnswerWindowMs, closed);
    check(closed.size() == 1 && closed[0].anomalous(), "fewer answers than routed hits is an anomaly");

    closed.clear();
    ledger.fired(9000, none());
    ledger.expire(9000 + kAnswerWindowMs, closed);
    check(closed.size() == 1 && !closed[0].anomalous(), "a shot that hit nothing is normal");

    closed.clear();
    SkipSighting sighting;
    sighting.count = 1;
    ledger.fired(12000, sighting);
    ledger.expire(12000 + kAnswerWindowMs, closed);
    check(closed.size() == 1 && closed[0].anomalous(), "a shot with a skipping living enemy is an anomaly even without a hit");
}

void testShotWindows() {
    ShotLedger ledger;
    std::vector<Shot> closed;
    ledger.fired(1000, none());
    ledger.routed(1000 + kHitWindowMs + 1, false);
    ledger.expire(1000 + kAnswerWindowMs, closed);
    check(closed.size() == 1 && closed[0].routed == 0, "a hit after the hit window belongs to no shot");

    closed.clear();
    ledger.fired(2000, none());
    ledger.fired(2100, none());
    ledger.routed(2200, true);
    ledger.refused(2200);
    ledger.expire(2000 + kAnswerWindowMs + 100, closed);
    check(closed.size() == 2 && closed[0].routed == 0 && closed[1].routed == 1 && closed[1].applied == 1 && closed[1].refused == 1,
          "hits go to the newest shot, a local answer counts at once");
}

void testShotCapacity() {
    ShotLedger ledger;
    std::vector<Shot> closed;
    for (uint32_t index = 0; index < kMaxOpenShots + 3; ++index) ledger.fired(1000 + index, none());
    ledger.expire(1000 + kMaxOpenShots + 3 + kAnswerWindowMs, closed);
    check(closed.size() == kMaxOpenShots, "the ledger keeps the newest kMaxOpenShots");
}

}  // namespace

int main() {
    testTracker();
    testTrackerBlip();
    testTrackerWraps();
    testSlotTrackers();
    testCorrectedFields();
    testValues();
    testLineBudget();
    testSummary();
    testShots();
    testShotWindows();
    testShotCapacity();
    if (g_failures == 0) std::printf("PASS\n");
    return g_failures == 0 ? 0 : 1;
}
