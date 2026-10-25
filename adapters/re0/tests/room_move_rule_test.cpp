// Checks the pure rules of the native rooms (no game). Exit 0 when every check passes.
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "../src/room_move_rule.h"

namespace {

namespace rule = room_move_rule;
using rule::Reason;
using rule::Result;

int g_failures = 0;

void check(bool ok, const char* what) {
    if (ok) return;
    std::printf("FAIL: %s\n", what);
    ++g_failures;
}

room_move_wire::RoomMove moveWithSeq(uint32_t seq) { return {seq, 0x2d, 1, 1, 0, 0}; }

constexpr uint16_t kHall = 0x2a;
constexpr uint16_t kLab = 0x2d;
constexpr uint16_t kCellar = 0x30;

void testClassify() {
    check(rule::classify(kHall, kHall, kLab) == rule::Kind::Enter, "entering the loaded room is an enter");
    check(rule::classify(kHall, kHall, kHall) == rule::Kind::Enter, "a move into the room it is in is still an enter");
    check(rule::classify(kLab, kHall, kHall) == rule::Kind::Leave, "leaving the loaded room for another is a leave");
    check(rule::classify(kCellar, kHall, kLab) == rule::Kind::Between, "a move between two other rooms is between");
    check(std::strcmp(rule::name(rule::Kind::Leave), "leave") == 0, "leave has its name");
    check(std::strcmp(rule::name(rule::Kind::Enter), "enter") == 0, "enter has its name");
    check(std::strcmp(rule::name(rule::Kind::Between), "between") == 0, "between has its name");
}

void testInbox() {
    rule::MoveInbox inbox;
    check(!inbox.pending(), "an empty inbox has nothing pending");
    check(inbox.offer(moveWithSeq(10)), "the first move is taken");
    check(inbox.pending() && inbox.pending()->seq == 10, "it is pending");
    check(inbox.offer(moveWithSeq(11)) && inbox.pending()->seq == 11, "a newer move replaces it");
    check(!inbox.offer(moveWithSeq(11)), "a repeat is dropped");
    check(!inbox.offer(moveWithSeq(9)), "an older move is dropped");
    inbox.consume();
    check(!inbox.pending(), "a consumed move is gone");
    check(!inbox.offer(moveWithSeq(11)), "a consumed sequence is still known");
    check(inbox.offer(moveWithSeq(12)), "the next one is taken");
    inbox.clear();
    check(!inbox.pending() && inbox.offer(moveWithSeq(1)), "a cleared inbox starts over");

    rule::MoveInbox wrapping;
    wrapping.offer(moveWithSeq(0xfffffffe));
    check(wrapping.offer(moveWithSeq(1)) && wrapping.pending()->seq == 1, "a sequence past the wrap is newer");
    check(!wrapping.offer(moveWithSeq(0xffffffff)), "one before the wrap is older");
}

void testSelfHeal() {
    rule::SelfHeal heal;
    constexpr uint32_t t0 = 5000;
    check(!heal.due(kLab, kLab, true, t0), "matching rooms never heal");
    check(!heal.due(kLab, kHall, true, t0), "the first difference starts the wait");
    check(!heal.due(kLab, kHall, true, t0 + rule::kSelfHealMs - 1), "just under the wait does not heal");
    check(heal.due(kLab, kHall, true, t0 + rule::kSelfHealMs), "the wait over heals");
    check(!heal.due(kLab, kHall, true, t0 + rule::kSelfHealMs + 1), "the next second is a new wait");
    check(heal.due(kLab, kHall, true, t0 + 2 * rule::kSelfHealMs), "a difference that lasts heals again");

    rule::SelfHeal changing;
    changing.due(kLab, kHall, true, t0);
    check(!changing.due(kCellar, kHall, true, t0 + rule::kSelfHealMs), "a pose room that changed restarts the wait");
    check(changing.due(kCellar, kHall, true, t0 + 2 * rule::kSelfHealMs), "and heals to the new room");

    rule::SelfHeal settling;
    settling.due(kLab, kHall, true, t0);
    check(!settling.due(kLab, kHall, false, t0 + rule::kSelfHealMs), "a door or event running never heals");
    check(!settling.due(kLab, kHall, true, t0 + rule::kSelfHealMs + 1), "and restarts the wait");

    rule::SelfHeal matching;
    matching.due(kLab, kHall, true, t0);
    matching.due(kLab, kLab, true, t0 + 500);
    check(!matching.due(kLab, kHall, true, t0 + rule::kSelfHealMs), "agreeing in between restarts the wait");

    rule::SelfHeal moved;
    moved.due(kLab, kHall, true, t0);
    moved.noteMove();
    check(!moved.due(kLab, kHall, true, t0 + rule::kSelfHealMs), "a ROOM_MOVE restarts the wait");

    rule::SelfHeal unknown;
    unknown.due(scene::kNone, kHall, true, t0);
    check(!unknown.due(scene::kNone, kHall, true, t0 + rule::kSelfHealMs), "a pose room still loading never heals");
    unknown.due(scene::kSceneIdLimit, kHall, true, t0);
    check(!unknown.due(scene::kSceneIdLimit, kHall, true, t0 + rule::kSelfHealMs), "a demo room never heals");
    unknown.due(kLab, scene::kNone, true, t0);
    check(!unknown.due(kLab, scene::kNone, true, t0 + rule::kSelfHealMs), "a puppet without a record never heals");

    rule::SelfHeal wrapped;
    wrapped.due(kLab, kHall, true, 0xfffffff0u);
    check(wrapped.due(kLab, kHall, true, 0xfffffff0u + rule::kSelfHealMs), "the wait counts across the clock wrap");
}

void testShouldRequest() {
    const rule::RunnerContext all{true, true, false, true};
    check(rule::shouldRequest(all), "a TEAM door with the partner in the room asks");
    rule::RunnerContext c = all;
    c.linked = false;
    check(!rule::shouldRequest(c), "nobody to ask");
    c = all;
    c.team = false;
    check(!rule::shouldRequest(c), "LEAVE_BEHIND never asks");
    c = all;
    c.leftBehind = true;
    check(!rule::shouldRequest(c), "a script that left the partner behind never asks");
    c = all;
    c.partnerInRoom = false;
    check(!rule::shouldRequest(c), "a partner in another room is not asked");
}

void testReason() {
    check(rule::reasonFor(room_phase::Main) == Reason::Team, "a door in plain play is a team door");
    check(rule::reasonFor(room_phase::Event) == Reason::Script, "a door inside an event is a script's");
    check(rule::reasonFor(room_phase::EventDemo) == Reason::Script, "a door after a demo is a script's");
}

rule::FollowContext freeContext() { return {true, true, false, false, room_phase::Main, false, 0, 0}; }

void testJudge() {
    check(rule::judge(Reason::Team, freeContext()) == Result::Commit, "free movement in the source room commits");
    rule::FollowContext c = freeContext();
    for (uint8_t state = 0; state <= rule::kLastFollowThinkState; ++state) {
        c.thinkState = state;
        check(rule::judge(Reason::Team, c) == Result::Commit, "think states 0..6 commit");
    }
    c.thinkState = rule::kLastFollowThinkState + 1;
    check(rule::judge(Reason::Team, c) == Result::Busy, "an action state is busy");
    c = freeContext();
    c.thinkGroup = 1;
    check(rule::judge(Reason::Team, c) == Result::Busy, "another think group is busy");
    c = freeContext();
    c.linked = false;
    check(rule::judge(Reason::Team, c) == Result::NoPeer, "without a peer there is no peer");
    c = freeContext();
    c.inSourceRoom = false;
    check(rule::judge(Reason::Team, c) == Result::OtherRoom, "another room is another room");
    c = freeContext();
    c.doorRunning = true;
    check(rule::judge(Reason::Team, c) == Result::Busy, "a door of its own is busy");
    c = freeContext();
    c.leadsThread = true;
    check(rule::judge(Reason::Team, c) == Result::Busy, "a led script thread is busy");
    c = freeContext();
    c.menuOpen = true;
    check(rule::judge(Reason::Team, c) == Result::Busy, "an open menu is busy");
    c = freeContext();
    c.roomPhase = room_phase::SubScreen;
    check(rule::judge(Reason::Team, c) == Result::Busy, "a sub screen is busy");
    c = freeContext();
    c.roomPhase = room_phase::Event;
    check(rule::judge(Reason::Team, c) == Result::Busy, "a team door is not followed in an event");
    check(rule::judge(Reason::Script, c) == Result::Commit, "a script's door is followed in the event");
    c.roomPhase = room_phase::EventDemo;
    check(rule::judge(Reason::Script, c) == Result::Busy, "but not in a demo");
    c = freeContext();
    c.linked = false;
    c.inSourceRoom = false;
    check(rule::judge(Reason::Team, c) == Result::NoPeer, "no peer is judged before the room");
}

void testSimultaneousDoors() {
    // Both players press a TEAM door in the same second: each side runs its own door when the other's request arrives.
    rule::FollowContext first = freeContext();
    first.doorRunning = true;
    rule::FollowContext second = freeContext();
    second.doorRunning = true;
    check(rule::judge(Reason::Team, first) == Result::Busy && rule::judge(Reason::Team, second) == Result::Busy,
          "two doors at once reject each other");
}

void testTracker() {
    rule::FollowTracker tracker;
    check(tracker.onResult(1, Result::Commit) == rule::FollowTracker::Outcome::Ignored, "no request, no outcome");
    tracker.begin(4, 1000);
    check(tracker.onResult(5, Result::Commit) == rule::FollowTracker::Outcome::Ignored, "another serial is ignored");
    check(tracker.onResult(4, Result::Commit) == rule::FollowTracker::Outcome::Accepted, "a commit is accepted");
    check(tracker.onResult(4, Result::Commit) == rule::FollowTracker::Outcome::Ignored, "an answer is taken once");

    tracker.begin(6, 1000);
    check(tracker.onResult(6, Result::Busy) == rule::FollowTracker::Outcome::Rejected, "busy is a rejection");
    tracker.begin(7, 1000);
    check(tracker.onResult(7, Result::OtherRoom) == rule::FollowTracker::Outcome::Rejected, "other room is a rejection");

    tracker.begin(8, 1000);
    check(!tracker.expire(1000 + rule::kFollowTimeoutMs - 1), "an unanswered request waits");
    const auto expired = tracker.expire(1000 + rule::kFollowTimeoutMs);
    check(expired && *expired == 8, "then it times out with its serial");
    check(!tracker.expire(1000 + 2 * rule::kFollowTimeoutMs), "a timeout is reported once");
    check(tracker.onResult(8, Result::Commit) == rule::FollowTracker::Outcome::Ignored, "a late answer is ignored");

    tracker.begin(9, 0xfffffff0u);
    check(tracker.expire(0xfffffff0u + rule::kFollowTimeoutMs).has_value(), "the timeout counts across the clock wrap");
    tracker.begin(10, 5);
    tracker.begin(11, 6);
    check(tracker.onResult(10, Result::Commit) == rule::FollowTracker::Outcome::Ignored, "a newer request replaces the older");
    tracker.clear();
    check(!tracker.expire(6 + rule::kFollowTimeoutMs), "a cleared tracker has nothing to time out");
}

void testArrival() {
    rule::ArrivalWatch watch;
    check(!watch.arrived(kLab), "nothing awaited, nothing arrives");
    watch.arm(kLab, 100);
    check(!watch.arrived(kHall), "another room is not the awaited one");
    check(watch.arrived(kLab), "the awaited room arrives");
    check(!watch.arrived(kLab), "and only once");
    watch.arm(kLab, 100);
    check(!watch.expired(100 + rule::kFollowTimeoutMs - 1), "a room change that is slow is still awaited");
    check(watch.expired(100 + rule::kFollowTimeoutMs), "one that never comes expires");
    check(!watch.expired(100 + 2 * rule::kFollowTimeoutMs) && !watch.arrived(kLab), "and is forgotten");
}

void testStale() {
    check(!rule::requestStale(1000, 1000 + rule::kFollowTimeoutMs - 1), "a recent request is judged");
    check(rule::requestStale(1000, 1000 + rule::kFollowTimeoutMs), "an old one is dropped");
    check(rule::requestStale(0xfffffff0u, 0xfffffff0u + rule::kFollowTimeoutMs), "age counts across the clock wrap");
}

void testInLoadedRoom() {
    constexpr uint32_t kOtherFloor = 1u << 3;  // scene 0x32: the controlled character on the other floor
    constexpr uint32_t kScriptHidden = 1u << 1;
    constexpr uint32_t kCameraPlane = 1u << 0;
    check(rule::inLoadedRoom(true, 0), "a shown character in the loaded record is in the room");
    check(rule::inLoadedRoom(true, kOtherFloor), "one hidden on the other floor is still in the room");
    check(rule::inLoadedRoom(true, kScriptHidden | kCameraPlane), "one a script or the camera hides is still in the room");
    check(!rule::inLoadedRoom(true, rule::kHiddenRoomNotLoaded | kOtherFloor), "one whose room is not loaded is not");
    check(!rule::inLoadedRoom(false, 0), "one in another record is not, whatever its mask says");
}

}  // namespace

int main() {
    testClassify();
    testInbox();
    testSelfHeal();
    testShouldRequest();
    testReason();
    testJudge();
    testSimultaneousDoors();
    testTracker();
    testArrival();
    testStale();
    testInLoadedRoom();
    if (g_failures == 0) std::printf("room_move_rule_test: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
