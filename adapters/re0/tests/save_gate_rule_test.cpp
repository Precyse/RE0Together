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
    check(rule::judge({std::nullopt, false, false}, kNow) == rule::Verdict::NoPose, "no pose waits");
    check(rule::judge({standing(0, 0, kNow - peer_sight::kFreshMs - 1), false, false}, kNow) == rule::Verdict::StalePose,
          "a stale pose waits");
    check(rule::judge({standing(0, 0, kNow - peer_sight::kFreshMs), false, false}, kNow) == rule::Verdict::Plain,
          "a pose at the freshness limit counts");
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
    testNames();
    if (g_failures == 0) std::printf("save_gate_rule_test: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
