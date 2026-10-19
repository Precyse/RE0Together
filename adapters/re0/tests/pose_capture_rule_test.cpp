// Checks the owner side's timing rules (no game): the 30 Hz sample accumulator and the held resend. Exit 0 when every
// check passes.
#include <cstdio>

#include "../src/pose_capture_rule.h"

namespace {

namespace rule = pose_capture_rule;

constexpr uint32_t kFrame60Ms = 17;  // the game moves a character about 60 times a second
constexpr uint32_t kFrame30Ms = 33;
constexpr uint32_t kSecondMs = 1000;
int g_failures = 0;

void check(bool ok, const char* what) {
    if (ok) return;
    std::printf("FAIL: %s\n", what);
    ++g_failures;
}

// Samples taken over `seconds` of frames `frameMs` apart.
uint32_t samplesOver(uint32_t frameMs, uint32_t seconds) {
    rule::SampleTimer timer;
    uint32_t samples = 0;
    for (uint32_t now = 1000; now < 1000 + seconds * kSecondMs; now += frameMs) {
        if (timer.due(now)) ++samples;
    }
    return samples;
}

void testSampleRate() {
    const uint32_t at60 = samplesOver(kFrame60Ms, 10);
    check(at60 >= 290 && at60 <= 310, "60 frames a second sample about 30 times a second");
    const uint32_t at30 = samplesOver(kFrame30Ms, 10);
    check(at30 >= 290 && at30 <= 310, "a capped 30 frames a second still samples about 30 times a second");
}

void testSampleStart() {
    rule::SampleTimer timer;
    check(timer.due(5000), "the first move samples");
    check(!timer.due(5010), "the next move inside the interval does not");
    check(timer.due(5000 + rule::kSampleIntervalMs), "the interval's end does");
}

void testLateMoveOwesNothing() {
    rule::SampleTimer timer;
    timer.due(1000);
    check(timer.due(3000), "a move after a long hitch samples");
    check(!timer.due(3010), "but the missed samples are not paid back");
    check(timer.due(3000 + rule::kSampleIntervalMs), "the rhythm continues from the hitch");
}

void testHeldResend() {
    rule::HeldResend held;
    check(!held.due(1000), "nothing is resent before a sample exists");
    held.onSample(1000);
    check(!held.due(1000 + rule::kHeldAfterMs - 1), "no resend while samples are recent");
    check(held.due(1000 + rule::kHeldAfterMs), "a resend is due after kHeldAfterMs without a sample");
    check(!held.due(1000 + rule::kHeldAfterMs + 1), "and not again at once");
    check(held.due(1000 + rule::kHeldAfterMs + rule::kHeldIntervalMs), "it repeats at 4 Hz");
    held.onSample(5000);
    check(!held.due(5000 + rule::kHeldAfterMs - 1), "a new sample ends the held phase");
    check(held.due(5000 + rule::kHeldAfterMs), "and starts the wait again");
}

void testHeldRate() {
    rule::HeldResend held;
    held.onSample(0);
    uint32_t resends = 0;
    for (uint32_t now = 1; now <= 5 * kSecondMs; ++now) {
        if (held.due(now)) ++resends;
    }
    check(resends >= 18 && resends <= 20, "a 5 s pause is about 4 resends a second");
}

}  // namespace

int main() {
    testSampleRate();
    testSampleStart();
    testLateMoveOwesNothing();
    testHeldResend();
    testHeldRate();
    if (g_failures == 0) std::printf("pose_capture_rule_test: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
