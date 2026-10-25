// Checks the wait notice rules: when a loading partner holds the world and what each notice says (no game). Exit 0 when
// every check passes.
#include <cmath>
#include <cstdio>

#include "../src/room_phase.h"
#include "../src/wait_notice_rule.h"

namespace {

namespace rule = wait_notice_rule;
using rule::Notice;

int g_failures = 0;

void check(bool ok, const char* what) {
    if (ok) return;
    std::printf("FAIL: %s\n", what);
    ++g_failures;
}

void testLoadHold() {
    check(rule::loadHolds(true, 0), "a partner loading the session holds a game that just started");
    check(rule::loadHolds(true, rule::kLoadWaitWindowMs - 1), "up to the window");
    check(!rule::loadHolds(true, rule::kLoadWaitWindowMs), "not a game already running (a mid-game join)");
    check(!rule::loadHolds(false, 0), "no loading partner, no hold");
}

void testNotices() {
    check(rule::holdNotice(room_phase::Message) == Notice::PartnerBusy, "a message the partner reads is busy");
    check(rule::holdNotice(room_phase::EventDemo) == Notice::PartnerBusy, "a cutscene is busy");
    check(rule::holdNotice(room_phase::Option) == Notice::PartnerPaused, "the pause menu is a pause");
    check(rule::text(Notice::PartnerPaused, "Kestrel") == "Kestrel has paused", "the pause names the partner");
    check(rule::text(Notice::PartnerPaused, "") == "Your partner has paused", "a pause without a lobby name");
    check(rule::text(Notice::PartnerBusy, "Kestrel") == "Waiting for Kestrel", "busy names the partner");
    check(rule::text(Notice::PartnerLoading, "Kestrel") == "Waiting for partner", "loading is generic");
    check(rule::text(Notice::None, "Kestrel").empty(), "no notice, no text");
}

void testMotion() {
    check(std::fabs(rule::pulseAlpha(0.0f) - rule::kPulseMinAlpha) < 1e-4f, "the pulse starts dim");
    check(std::fabs(rule::pulseAlpha(rule::kPulseSeconds * 0.5f) - rule::kPulseMaxAlpha) < 1e-4f, "and peaks at half a period");
    check(std::fabs(rule::pulseAlpha(rule::kPulseSeconds) - rule::kPulseMinAlpha) < 1e-4f, "then comes back");
    check(rule::pulseAlpha(0.7f) >= rule::kPulseMinAlpha && rule::pulseAlpha(0.7f) <= rule::kPulseMaxAlpha, "and stays between");
    check(std::fabs(rule::sweepStart(0.0f, 140.0f, 46.0f)) < 1e-4f, "the segment starts at the left end");
    check(std::fabs(rule::sweepStart(rule::kSweepSeconds * 0.5f, 140.0f, 46.0f) - 47.0f) < 1e-3f, "and is halfway at half a period");
    check(rule::sweepStart(rule::kSweepSeconds * 0.999f, 140.0f, 46.0f) <= 94.0f, "and never leaves the hairline");
}

}  // namespace

int main() {
    testLoadHold();
    testNotices();
    testMotion();
    if (g_failures == 0) std::printf("wait_notice_rule_test: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
