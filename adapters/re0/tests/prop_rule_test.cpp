// Checks the push block rules (no game). Exit 0 when every check passes.
#include <cmath>
#include <cstdio>
#include <numbers>

#include "../src/prop_rule.h"

namespace {

namespace rule = prop_rule;
using rule::Key;
using rule::Pose;

int g_failures = 0;

void check(bool ok, const char* what) {
    if (ok) return;
    std::printf("FAIL: %s\n", what);
    ++g_failures;
}

constexpr Key kBox{0x2d, 1};
constexpr uint8_t kHost = 0;
constexpr uint8_t kGuest = 1;

Pose at(float x, float yaw = 0.0f) { return {{x, 0.0f, 0.0f}, yaw}; }

void testYaw() {
    float quat[4];
    rule::quatOf(1.2f, quat);
    check(std::fabs(rule::yawOf(quat) - 1.2f) < 1e-5f, "a yaw survives the quaternion");
    check(rule::yawGap(3.1f, -3.1f) < 0.1f, "yaw gaps wrap around");
    check(rule::differs(at(0), at(1)), "a unit of movement differs");
    check(!rule::differs(at(0), at(0.1f)), "a tenth of a unit does not");
    check(rule::differs(at(0, 0.0f), at(0, 0.5f)), "a turn differs");
}

void testLeaseGrantAndDeny() {
    rule::LeaseTable table;
    const rule::Decision first = table.request(kBox, kGuest, 1000);
    check(first.granted && first.serial == 1 && first.holder == kGuest, "the first asker holds the box");
    const rule::Decision second = table.request(kBox, kHost, 1100);
    check(!second.granted && second.holder == kGuest && second.serial == first.serial, "the other asker is told who holds it");
    const rule::Decision again = table.request(kBox, kGuest, 1200);
    check(again.granted && again.serial == first.serial, "the holder asking again keeps its lease and serial");
    check(table.request({0x2d, 2}, kHost, 1200).serial == 2, "another box is another lease with the next serial");
    check(table.holderOf(kBox, 1300) == kGuest, "the table names the holder");
}

void testLeaseLapse() {
    rule::LeaseTable table;
    table.request(kBox, kGuest, 1000);
    check(table.holderOf(kBox, 1000 + rule::kLeaseIdleMs - 1) == kGuest, "a lease is held until it is idle long enough");
    check(!table.holderOf(kBox, 1000 + rule::kLeaseIdleMs), "an idle lease lapses");
    const rule::Decision taken = table.request(kBox, kHost, 1000 + rule::kLeaseIdleMs);
    check(taken.granted && taken.holder == kHost && taken.serial == 2, "a lapsed lease is up for grabs");
}

void testLeaseExpire() {
    rule::LeaseTable table;
    table.request(kBox, kGuest, 1000);
    table.acceptMove(kBox, kGuest, 50, at(5), 1500);
    table.request({0x2d, 2}, kHost, 4000);
    const auto gone = table.expire(1500 + rule::kLeaseIdleMs);
    check(gone.size() == 1 && gone[0].key == kBox && gone[0].holder == kGuest, "only the idle lease expires");
    check(gone[0].hasPose && gone[0].pose.pos[0] == 5.0f, "an expired lease hands back the last pose it had");
    check(table.size() == 1, "the live lease stays");
}

void testMoves() {
    rule::LeaseTable table;
    table.request(kBox, kGuest, 1000);
    check(table.acceptMove(kBox, kHost, 10, at(1), 1100) == rule::MoveResult::NotHolder, "a move from a non-holder is refused");
    check(table.acceptMove({0x2d, 3}, kGuest, 10, at(1), 1100) == rule::MoveResult::NotHolder, "a move for an unleased box is refused");
    check(table.acceptMove(kBox, kGuest, 10, at(1), 1100) == rule::MoveResult::Accepted, "the holder's move is taken");
    check(table.acceptMove(kBox, kGuest, 10, at(2), 1110) == rule::MoveResult::Stale, "the same move twice is stale");
    check(table.acceptMove(kBox, kGuest, 5, at(0), 1120) == rule::MoveResult::Stale, "an older move is stale");
    check(table.acceptMove(kBox, kGuest, 20, at(3), 1130) == rule::MoveResult::Accepted, "a newer move is taken");
    check(table.acceptMove(kBox, kGuest, 0xfffffff0u, at(3), 1140) == rule::MoveResult::Stale, "order is wrap-safe");
    table.touch(kBox, kGuest, 4000);
    check(table.holderOf(kBox, 4000 + rule::kLeaseIdleMs - 1) == kGuest, "a keep-alive renews the lease");
}

void testRelease() {
    rule::LeaseTable table;
    table.request(kBox, kGuest, 1000);
    table.acceptMove(kBox, kGuest, 10, at(7), 1100);
    check(!table.release(kBox, kHost), "a non-holder cannot let go");
    const auto held = table.release(kBox, kGuest);
    check(held && held->hasPose && held->pose.pos[0] == 7.0f && held->serial == 1, "the holder lets go with its last pose");
    check(!table.holderOf(kBox, 1200) && table.size() == 0, "the box is free after");
    check(!table.release(kBox, kGuest), "nothing to let go twice");
    table.adopt(kBox, kHost, 5, 2000);
    check(table.holderOf(kBox, 2100) == kHost, "a granted lease the host announced is adopted");
    check(table.drop(kBox) && !table.holderOf(kBox, 2100), "a commit ends whatever lease there was");
    check(!table.drop(kBox), "a commit with no lease changes nothing");
}

void testLeaseBound() {
    rule::LeaseTable table;
    for (uint8_t unit = 0; unit < 5; ++unit) {
        for (uint16_t scene = 0; scene < 5; ++scene) table.adopt({scene, unit}, kHost, 1, 0);
    }
    check(table.size() == rule::kMaxLeases, "the table is bounded");
}

void testJournal() {
    rule::Journal journal;
    check(!journal.lastCommitter(kBox), "nobody pushed an untouched box");
    journal.note({kBox, 3, kGuest, at(1)});
    check(journal.lastCommitter(kBox) == kGuest, "the committer is remembered");
    journal.note({kBox, 2, kHost, at(2)});
    check(journal.lastCommitter(kBox) == kGuest, "an older commit does not replace a newer one");
    journal.note({kBox, 4, kHost, at(3)});
    check(journal.lastCommitter(kBox) == kHost && journal.records().size() == 1, "a newer commit replaces it");
    journal.note({{0x2e, 0}, 1, kGuest, at(0)});
    journal.forgetScene(0x2d);
    check(!journal.lastCommitter(kBox) && journal.records().size() == 1, "a reloaded room forgets its own commits only");
    for (uint16_t scene = 0; scene < 100; ++scene) journal.note({{scene, 0}, 1, kHost, at(0)});
    check(journal.records().size() == rule::kMaxJournal, "the journal is bounded");
}

void testOwner() {
    using event_rule::PropOwner;
    check(rule::ownerOf(std::nullopt, kHost) == PropOwner::Unknown, "no pusher: unknown");
    check(rule::ownerOf(kHost, kHost) == PropOwner::Mine, "this machine pushed");
    check(rule::ownerOf(kGuest, kHost) == PropOwner::Theirs, "the other machine pushed");
}

rule::Observation pushing(uint32_t nowMs, float x) {
    rule::Observation now;
    now.pushing = true;
    now.key = kBox;
    now.pose = at(x);
    now.nowMs = nowMs;
    return now;
}

rule::Observation idle(uint32_t nowMs) {
    rule::Observation now;
    now.nowMs = nowMs;
    return now;
}

void testPusherGranted() {
    rule::PushTracker push;
    check(!push.observe(idle(0)).request, "not pushing, nothing asked");
    rule::Step step = push.observe(pushing(100, 0));
    check(step.request && step.key == kBox && push.phase() == rule::PushTracker::Phase::Asked, "a push asks for the lease");
    check(!push.observe(pushing(116, 0)).request && !push.observe(pushing(133, 0)).move, "no second ask and no move before the answer");
    push.answered(kBox, true);
    step = push.observe(pushing(150, 1));
    check(step.move && step.pose.pos[0] == 1.0f, "the first pose goes at once once granted");
    check(!push.observe(pushing(166, 2)).move, "not again before the interval");
    check(push.observe(pushing(150 + rule::kMoveIntervalMs, 3)).move, "a moving box is sent at 10 Hz");
    check(!push.observe(pushing(250 + 50, 3)).move, "a box that stopped is not sent every interval");
    check(push.observe(pushing(250 + rule::kKeepAliveMs, 3)).move, "a box that stopped is kept alive");
    step = push.observe(idle(2000));
    check(step.end && !step.cancel && step.key == kBox, "letting go ends the push");
    check(push.phase() == rule::PushTracker::Phase::Idle, "ready for the next push");
}

void testPusherDeniedAndCancelled() {
    rule::PushTracker denied;
    denied.observe(pushing(0, 0));
    denied.answered(kBox, false);
    check(!denied.observe(pushing(100, 5)).move, "a denied push sends nothing");
    const rule::Step ended = denied.observe(idle(200));
    check(!ended.end && !ended.cancel, "and says nothing when it ends");

    rule::PushTracker cancelled;
    cancelled.observe(pushing(0, 0));
    const rule::Step step = cancelled.observe(idle(50));
    check(step.cancel && !step.end, "a push that ends before the answer cancels its request");

    rule::PushTracker late;
    late.observe(pushing(0, 0));
    const rule::Step timeout = late.observe(pushing(rule::kRequestTimeoutMs, 0));
    check(timeout.timedOut && late.phase() == rule::PushTracker::Phase::Denied, "an unanswered request times out");
    late.answered(kBox, true);
    check(late.phase() == rule::PushTracker::Phase::Denied, "a late answer changes nothing");

    rule::PushTracker stranger;
    stranger.observe(pushing(0, 0));
    stranger.answered({0x2d, 2}, true);
    check(stranger.phase() == rule::PushTracker::Phase::Asked, "an answer for another box is ignored");
}

stream_clock::StreamClock readyClock() {
    stream_clock::StreamClock clock;
    clock.observe(1000, 1000);  // zero transit time: the shown time is the render delay behind the sender's
    return clock;
}

void testTrackInterpolates() {
    const stream_clock::StreamClock clock = readyClock();
    rule::Track track;
    check(track.empty() && !track.render(clock, 5000).valid, "nothing before the first move");
    track.push(clock, {2000, at(0), false});
    track.push(clock, {2100, at(10), false});
    track.push(clock, {2100, at(99), false});
    const uint32_t delay = clock.delayMs();
    check(!track.render(clock, 2000 + delay - 1).valid, "nothing before the first move's time");
    rule::Shown shown = track.render(clock, 2100 + delay);
    check(shown.valid && std::fabs(shown.pose.pos[0] - 10.0f) < 0.01f && !shown.finished, "the stream is shown one render delay late");
    track.push(clock, {2200, at(20), false});
    shown = track.render(clock, 2150 + delay);
    check(shown.valid && std::fabs(shown.pose.pos[0] - 15.0f) < 0.01f, "between two moves the box is between them");
    check(track.render(clock, 2150 + delay).valid, "rendering twice is stable");
}

void testTrackFinishes() {
    const stream_clock::StreamClock clock = readyClock();
    rule::Track track;
    track.push(clock, {2000, at(0), false});
    track.push(clock, {2100, at(10), false});
    track.finish(&clock, at(12));
    track.push(clock, {2300, at(50), false});
    check(!track.render(clock, 2150).finished, "the commit is not shown before its time");
    rule::Shown shown = track.render(clock, 2300);
    check(shown.valid && shown.finished && shown.pose.pos[0] == 12.0f, "the commit is the last pose and ends the track");
    check(track.render(clock, 9000).finished, "and stays the last pose");

    rule::Track bare;
    bare.finish(nullptr, at(7));
    shown = bare.render(clock, 100);
    check(shown.valid && shown.finished && shown.pose.pos[0] == 7.0f, "a commit with no stream is shown at once");
    rule::Track unclocked;
    unclocked.push(clock, {2000, at(0), false});
    unclocked.finish(&clock, at(3));
    check(unclocked.render(clock, 2200).finished, "a commit after a single move follows it");
}

void testTrackClockRestart() {
    stream_clock::StreamClock clock = readyClock();
    rule::Track track;
    track.push(clock, {2000, at(0), false});
    clock.reanchor(0);
    track.push(clock, {500, at(5), false});
    const rule::Shown shown = track.render(clock, 700);
    check(shown.valid && shown.pose.pos[0] == 5.0f, "a restarted peer clock drops what was buffered");
}

void testTrackBound() {
    const stream_clock::StreamClock clock = readyClock();
    rule::Track track;
    for (uint32_t i = 0; i < 100; ++i) track.push(clock, {2000 + i * 10, at(static_cast<float>(i)), false});
    const rule::Shown shown = track.render(clock, 100000);
    check(shown.valid && shown.pose.pos[0] == 99.0f, "a long stream keeps its newest moves");
}

}  // namespace

int main() {
    testYaw();
    testLeaseGrantAndDeny();
    testLeaseLapse();
    testLeaseExpire();
    testMoves();
    testRelease();
    testLeaseBound();
    testJournal();
    testOwner();
    testPusherGranted();
    testPusherDeniedAndCancelled();
    testTrackInterpolates();
    testTrackFinishes();
    testTrackClockRestart();
    testTrackBound();
    if (g_failures == 0) std::printf("prop_rule_test: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
