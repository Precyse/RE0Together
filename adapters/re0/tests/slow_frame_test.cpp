// Checks the SLOW_FRAME rule (no game): the 50 ms threshold, the 5 s rate limit, per-category sums that reset every
// frame, and the name to category mapping of the game_tick entries. Exit 0 when every check passes.
#include <cstdio>

#include "../src/slow_frame_rule.h"

namespace {

using slow_frame::Category;
using slow_frame::Tracker;

constexpr uint64_t kMs = 1000;
constexpr uint64_t kSecond = 1'000'000;
constexpr uint64_t kStart = 10 * kSecond;
int g_failures = 0;

void check(bool ok, const char* what) {
    if (ok) return;
    std::printf("FAIL: %s\n", what);
    ++g_failures;
}

void testThreshold() {
    Tracker tracker;
    check(!tracker.beginFrame(kStart), "the first frame has no interval to report");
    check(!tracker.beginFrame(kStart + slow_frame::kSlowFrameUs - 1), "49.999 ms is not slow");
    Tracker exact;
    exact.beginFrame(kStart);
    check(exact.beginFrame(kStart + slow_frame::kSlowFrameUs), "exactly 50 ms is slow");
    check(exact.report().frameMs == 50, "the report holds the interval in ms");
}

// Quick 10 ms frames up to 60 ms before `endUs`, then one 60 ms frame ending there; true when it was reported.
bool slowFrameEndingAt(Tracker& tracker, uint64_t& now, uint64_t endUs) {
    constexpr uint64_t kQuickFrame = 10 * kMs;
    constexpr uint64_t kSlowFrame = 60 * kMs;
    while (now + kQuickFrame <= endUs - kSlowFrame) {
        now += kQuickFrame;
        tracker.beginFrame(now);
    }
    now = endUs - kSlowFrame;
    tracker.beginFrame(now);
    now = endUs;
    return tracker.beginFrame(now);
}

void testRateLimit() {
    Tracker tracker;
    uint64_t now = kStart;
    tracker.beginFrame(now);
    const uint64_t firstEnd = kStart + 100 * kMs;
    check(slowFrameEndingAt(tracker, now, firstEnd), "the first slow frame is reported");
    check(!slowFrameEndingAt(tracker, now, firstEnd + kSecond), "a slow frame 1 s later is not");
    check(!slowFrameEndingAt(tracker, now, firstEnd + slow_frame::kLogIntervalUs - 1), "just under 5 s after the report: still quiet");
    check(slowFrameEndingAt(tracker, now, firstEnd + slow_frame::kLogIntervalUs), "exactly 5 s after the report it is reported again");
}

void testSums() {
    Tracker tracker;
    tracker.beginFrame(kStart);
    tracker.add(Category::Net, 3 * kMs);
    tracker.add(Category::Puppet, 4 * kMs + 900);
    tracker.add(Category::Puppet, 2 * kMs);
    tracker.add(Category::Other, 1 * kMs);
    check(tracker.beginFrame(kStart + 80 * kMs), "a slow frame with timings");
    const slow_frame::Report& report = tracker.report();
    check(report.frameMs == 80, "frame ms");
    check(report.categoryMs[static_cast<size_t>(Category::Net)] == 3, "net ms");
    check(report.categoryMs[static_cast<size_t>(Category::Puppet)] == 6, "puppet ms adds the calls, then truncates to ms");
    check(report.categoryMs[static_cast<size_t>(Category::Other)] == 1, "other ms");
    check(report.categoryMs[static_cast<size_t>(Category::Fx)] == 0, "an untouched category is 0");
}

void testSumsResetAfterQuietFrame() {
    Tracker tracker;
    tracker.beginFrame(kStart);
    tracker.add(Category::Enemies, 40 * kMs);
    tracker.beginFrame(kStart + 10 * kMs);
    check(tracker.beginFrame(kStart + 10 * kMs + slow_frame::kSlowFrameUs), "the next frame is slow");
    check(tracker.report().categoryMs[static_cast<size_t>(Category::Enemies)] == 0, "the earlier frame's time is not carried over");
}

void testCategories() {
    static_assert(slow_frame::categoryOf("puppet") == Category::Puppet);
    static_assert(slow_frame::categoryOf("pose_capture") == Category::Puppet);
    static_assert(slow_frame::categoryOf("enemy_observer") == Category::Enemies);
    static_assert(slow_frame::categoryOf("weapon_fx_replay") == Category::Fx);
    static_assert(slow_frame::categoryOf("event_place") == Category::Events);
    static_assert(slow_frame::categoryOf("room_authority") == Category::Events);
    static_assert(slow_frame::categoryOf("net_pad") == Category::Net);
    static_assert(slow_frame::categoryOf("menu_mirror") == Category::Other);
    static_assert(slow_frame::categoryOf("move_scope") == Category::Other);
    static_assert(slow_frame::categoryOf("") == Category::Other);
    check(slow_frame::categoryOf("pup") == Category::Other, "a name shorter than a prefix does not match it");
}

}  // namespace

int main() {
    testThreshold();
    testRateLimit();
    testSums();
    testSumsResetAfterQuietFrame();
    testCategories();
    if (g_failures == 0) std::printf("slow_frame_test: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
