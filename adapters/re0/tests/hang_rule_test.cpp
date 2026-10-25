// Checks the hang watchdog's rule (no game): one Dump per stall, only while the window is foreground. Exit 0 when every
// check passes.
#include <cstdio>

#include "../src/hang_rule.h"

namespace {

using hang_rule::HangWatch;
using hang_rule::kHangMs;
using hang_rule::Verdict;

constexpr uint32_t kPollMs = 1000;
constexpr uint32_t kStart = 5000;
int g_failures = 0;

void check(bool ok, const char* what) {
    if (ok) return;
    std::printf("FAIL: %s\n", what);
    ++g_failures;
}

// Polls every kPollMs from `from` until `until` (inclusive); how many Dump verdicts came.
int dumpsBetween(HangWatch& watch, uint32_t from, uint32_t until, bool foreground) {
    int dumps = 0;
    for (uint32_t now = from; now - from <= until - from; now += kPollMs) {
        if (watch.check(now, foreground) == Verdict::Dump) ++dumps;
    }
    return dumps;
}

void testNothingBeforeTheFirstBeat() {
    HangWatch watch;
    check(dumpsBetween(watch, kStart, kStart + 3 * kHangMs, true) == 0, "no frame was ever drawn: nothing to judge");
}

void testBeatingGameIsNeverHung() {
    HangWatch watch;
    int dumps = 0;
    for (uint32_t now = kStart; now < kStart + 3 * kHangMs; now += kPollMs) {
        watch.beat(now);
        if (watch.check(now, true) == Verdict::Dump) ++dumps;
    }
    check(dumps == 0, "a frame every poll never dumps");
}

void testOneDumpPerStall() {
    HangWatch watch;
    watch.beat(kStart);
    check(dumpsBetween(watch, kStart + kPollMs, kStart + kHangMs - kPollMs, true) == 0, "no dump below the limit");
    check(watch.check(kStart + kHangMs, true) == Verdict::Dump, "the dump comes at the limit");
    check(dumpsBetween(watch, kStart + kHangMs + kPollMs, kStart + 5 * kHangMs, true) == 0, "one dump per stall");
}

void testBeatRearms() {
    HangWatch watch;
    watch.beat(kStart);
    check(watch.check(kStart + kHangMs, true) == Verdict::Dump, "first stall dumps");
    const uint32_t second = kStart + 2 * kHangMs;
    watch.beat(second);
    check(dumpsBetween(watch, second + kPollMs, second + kHangMs - kPollMs, true) == 0, "the beat cleared the stall time");
    check(watch.check(second + kHangMs, true) == Verdict::Dump, "the next stall dumps again");
}

void testBackgroundStallNeverDumps() {
    HangWatch watch;
    watch.beat(kStart);
    check(dumpsBetween(watch, kStart + kPollMs, kStart + 3 * kHangMs, false) == 0,
          "a game in the background may be throttled: no dump");
}

void testFocusRegainedContinuesTheCount() {
    HangWatch watch;
    watch.beat(kStart);
    const uint32_t half = kHangMs / 2;
    check(dumpsBetween(watch, kStart + kPollMs, kStart + half, true) == 0, "half the limit in the foreground");
    check(dumpsBetween(watch, kStart + half + kPollMs, kStart + 3 * kHangMs, false) == 0, "background time adds nothing");
    const uint32_t back = kStart + 3 * kHangMs + kPollMs;
    check(watch.check(back, true) == Verdict::Wait, "back in the foreground the count goes on from where it was");
    check(dumpsBetween(watch, back + kPollMs, back + kHangMs, true) == 1, "and reaches the limit");
}

void testWrapAroundTheClock() {
    HangWatch watch;
    const uint32_t nearWrap = 0xffffffffu - 5 * kPollMs;
    watch.beat(nearWrap);
    check(dumpsBetween(watch, nearWrap + kPollMs, nearWrap + kHangMs - kPollMs, true) == 0, "no dump below the limit across the wrap");
    check(watch.check(nearWrap + kHangMs, true) == Verdict::Dump, "the dump comes at the limit across the wrap");
}

}  // namespace

int main() {
    testNothingBeforeTheFirstBeat();
    testBeatingGameIsNeverHung();
    testOneDumpPerStall();
    testBeatRearms();
    testBackgroundStallNeverDumps();
    testFocusRegainedContinuesTheCount();
    testWrapAroundTheClock();
    if (g_failures == 0) std::printf("hang_rule_test: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
