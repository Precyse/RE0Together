// Checks which door-entry spot a character is placed on (no game). Exit 0 when every check passes.
#include <cstdio>

#include "../src/spot_rule.h"

namespace {

namespace rule = spot_rule;

int g_failures = 0;

void check(bool ok, const char* what) {
    if (ok) return;
    std::printf("FAIL: %s\n", what);
    ++g_failures;
}

}  // namespace

int main() {
    // Entry 0 of scene 0x37 as read live: door, side (+50 z), follower (-50 z).
    const rule::Spot distinct[rule::kModes] = {{1788, 300, 3869}, {1788, 300, 3919}, {1788, 300, 3819}};
    // Entry 1: the follower spot equals the door spot, the side spot is +70 x.
    const rule::Spot sameFollower[rule::kModes] = {{1933, 300, 4157}, {2003, 300, 4157}, {1933, 300, 4157}};
    const rule::Spot allAlike[rule::kModes] = {{10, 0, 10}, {10, 0, 10}, {10, 0, 10}};

    check(rule::modeFor(false, distinct) == rule::kDoorMode, "alone: the door spot");
    check(rule::modeFor(true, distinct) == rule::kFollowMode, "with the other one there: the follower spot");
    check(rule::modeFor(true, sameFollower) == rule::kSideMode, "follower spot equal to the door spot: the side spot");
    check(rule::modeFor(true, allAlike) == rule::kDoorMode, "no distinct spot: the door spot");
    if (g_failures == 0) std::printf("spot_rule_test: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
