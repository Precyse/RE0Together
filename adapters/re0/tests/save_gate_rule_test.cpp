// Checks the host's save gate rule (no game). Exit 0 when every check passes.
#include <cstdio>

#include "../src/save_gate_rule.h"

namespace {

namespace rule = save_gate_rule;
using peer_sight::Sight;

constexpr uint32_t kNow = 50000;
int g_failures = 0;

void check(bool ok, const char* what) {
    if (ok) return;
    std::printf("FAIL: %s\n", what);
    ++g_failures;
}

Sight standing(uint8_t group, uint8_t state, uint32_t arrival = kNow - 30) { return {0x2d, 1, group, state, arrival}; }

rule::Situation settled() { return {standing(0, 0), false, false}; }

void testPlain() {
    check(rule::judge(settled(), kNow) == rule::Verdict::Plain, "a partner standing still lets the save through");
    for (uint8_t state = 0; state <= room_move_rule::kLastFollowThinkState; ++state) {
        check(rule::judge({standing(0, state), false, false}, kNow) == rule::Verdict::Plain,
              "think group 0 states 0..6 are plain");
    }
}

void testBusy() {
    check(rule::judge({standing(0, 7), false, false}, kNow) == rule::Verdict::PartnerBusy, "an action state waits");
    check(rule::judge({standing(2, 0), false, false}, kNow) == rule::Verdict::PartnerBusy, "a hit reaction (group 2) waits");
    check(rule::judge({standing(3, 0), false, false}, kNow) == rule::Verdict::PartnerBusy, "a death (group 3) waits");
}

void testDoorAndScript() {
    check(rule::judge({standing(0, 0), true, false}, kNow) == rule::Verdict::PartnerInDoor, "a partner in a door waits");
    check(rule::judge({standing(0, 0), false, true}, kNow) == rule::Verdict::PartnerInScript, "a partner in a script waits");
    check(rule::judge({standing(0, 9), true, true}, kNow) == rule::Verdict::PartnerInDoor, "the door is named before the rest");
}

void testPose() {
    check(rule::judge({std::nullopt, false, false}, kNow) == rule::Verdict::Plain, "a partner with no pose has nothing changing");
    check(rule::judge({std::nullopt, true, true}, kNow) == rule::Verdict::Plain, "no pose is plain whatever else is reported");
    check(rule::judge({standing(0, 0, kNow - peer_sight::kFreshMs - 1), false, false}, kNow) == rule::Verdict::StalePose,
          "a stale pose waits");
    check(rule::judge({standing(0, 0, kNow - peer_sight::kFreshMs), false, false}, kNow) == rule::Verdict::Plain,
          "a pose at the freshness limit counts");
}

// The save screen asks once a frame; returns the first ask (ms after the start) that goes through, or -1.
int firstThrough(rule::Patience& patience, rule::Verdict verdict, uint32_t startMs, uint32_t spanMs) {
    constexpr uint32_t kFrameMs = 16;
    for (uint32_t elapsed = 0; elapsed <= spanMs; elapsed += kFrameMs) {
        if (patience.decide(verdict, startMs + elapsed) != rule::Decision::Wait) return static_cast<int>(elapsed);
    }
    return -1;
}

void testPatience() {
    rule::Patience patience;
    check(patience.decide(rule::Verdict::Plain, kNow) == rule::Decision::Save, "a plain verdict saves");
    check(patience.decide(rule::Verdict::PartnerBusy, kNow) == rule::Decision::Wait, "a refusal waits");
    rule::Patience frames;
    const int through = firstThrough(frames, rule::Verdict::StalePose, kNow, 2 * rule::kMaxWaitMs);
    check(through >= static_cast<int>(rule::kMaxWaitMs) && through < static_cast<int>(rule::kMaxWaitMs) + 16,
          "a screen asking every frame is let through after the limit");
    check(frames.decide(rule::Verdict::StalePose, kNow + through + 16) == rule::Decision::Wait,
          "the request after a pass starts a new wait");

    rule::Patience gap;
    gap.decide(rule::Verdict::PartnerBusy, kNow);
    check(gap.decide(rule::Verdict::PartnerBusy, kNow + rule::kMaxWaitMs + rule::kRequestGapMs) == rule::Decision::Wait,
          "an ask after a pause is a new request, not the old wait");

    rule::Patience reset;
    reset.decide(rule::Verdict::PartnerBusy, kNow);
    reset.decide(rule::Verdict::Plain, kNow + 1000);
    check(reset.decide(rule::Verdict::PartnerBusy, kNow + 1100) == rule::Decision::Wait &&
              reset.decide(rule::Verdict::PartnerBusy, kNow + rule::kMaxWaitMs) == rule::Decision::Wait,
          "a plain answer restarts the count");

    rule::Patience wrapped;
    constexpr uint32_t kNearWrap = 0xffffff00u;
    const int wrappedThrough = firstThrough(wrapped, rule::Verdict::PartnerBusy, kNearWrap, 2 * rule::kMaxWaitMs);
    check(wrappedThrough >= static_cast<int>(rule::kMaxWaitMs) && wrappedThrough < static_cast<int>(rule::kMaxWaitMs) + 16,
          "the wait survives the clock wrapping");
}

void testNames() {
    check(rule::name(rule::Verdict::PartnerBusy)[0] != '\0' && rule::name(rule::Verdict::Plain)[0] != '\0',
          "every verdict has a name");
}

}  // namespace

int main() {
    testPlain();
    testBusy();
    testDoorAndScript();
    testPose();
    testPatience();
    testNames();
    if (g_failures == 0) std::printf("save_gate_rule_test: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
