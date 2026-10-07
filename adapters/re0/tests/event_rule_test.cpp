// Checks the room event replication rules (no game). Exit 0 when every check passes.
#include <cstdio>

#include "../src/event_rule.h"
#include "../src/event_sync.h"

namespace {

namespace rule = event_rule;
using rule::Follow;
using rule::OpRole;
using rule::TriggerKind;

constexpr uint16_t kRoom = 0x25;
constexpr uint16_t kOtherRoom = 0x2c;
constexpr uint32_t kAnySubject = 0x10000;
constexpr uint32_t kPartnerSubject = 0x30000;

int g_failures = 0;

void check(bool ok, const char* what) {
    if (ok) return;
    std::printf("FAIL: %s\n", what);
    ++g_failures;
}

void testClassify() {
    check(rule::classify(2, 0) == TriggerKind::Local, "action in a zone (examine, doors) is the player's");
    check(rule::classify(31, 0) == TriggerKind::Local, "door trigger is the player's");
    check(rule::classify(10, 0) == TriggerKind::Local, "holding an item is the player's");
    check(rule::classify(6, 0) == TriggerKind::Local, "the entry a player came in by is that player's");
    check(rule::classify(8, 0x47) == TriggerKind::Local, "using an item in a zone is the player's");
    check(rule::classify(4, rule::kSubjectControlled) == TriggerKind::Local, "controlled character in a zone");
    check(rule::classify(4, kPartnerSubject) == TriggerKind::Shared, "partner in a zone is shared");
    check(rule::classify(5, kAnySubject) == TriggerKind::Shared, "any unit in a zone is shared");
    check(rule::classify(11, 0) == TriggerKind::Shared, "flag condition is shared");
    check(rule::classify(14, 6) == TriggerKind::Shared, "either character in a zone (the train crows) is shared");
    check(rule::classify(12, 0) == TriggerKind::Shared, "an enemy's death is shared");
    check(rule::classify(18, 0) == TriggerKind::Shared, "a timer is shared");
    check(rule::classify(37, 0) == TriggerKind::Shared, "the room-start delay is shared");
}

void testMayFire() {
    check(rule::mayFire(TriggerKind::Local, true, false), "a local trigger fires where it was met");
    check(rule::mayFire(TriggerKind::Shared, true, true), "the authority fires shared triggers");
    check(!rule::mayFire(TriggerKind::Shared, true, false), "the other machine does not");
    check(rule::mayFire(TriggerKind::Shared, false, false), "a player alone in a room fires everything");
}

void testOpRoles() {
    check(rule::roleOf(rule::op::kMes) == OpRole::LeaderOnly, "messages are the subject player's");
    check(rule::roleOf(rule::op::kMesSel) == OpRole::LeaderOnly, "selections are the subject player's");
    check(rule::roleOf(rule::op::kDoor) == OpRole::LeaderOnly, "doors go through door_sync");
    check(rule::roleOf(rule::op::kItemGet) == OpRole::LeaderOnly, "items go through inventory sync");
    check(rule::roleOf(rule::op::kItemPut) == OpRole::LeaderOnly, "floor items go through floor_items_sync");
    check(rule::roleOf(rule::op::kEventExec) == OpRole::LeaderOnly, "a fork arrives as its own start");
    check(rule::roleOf(rule::op::kCharChange2) == OpRole::Switch, "a character switch moves the lead, not the camera");
    check(rule::roleOf(rule::op::kTraceOff) == OpRole::Follow, "TraceOff sets the party, not the follow flag");
    check(rule::roleOf(rule::op::kSub) == OpRole::LeaderOnly, "an item selection is the subject player's");
    constexpr uint16_t kUpCutStart = 86;
    constexpr uint16_t kUpCutProc = 88;
    check(rule::roleOf(kUpCutStart) == OpRole::LeaderOnly, "a close-up is the subject player's");
    check(rule::roleOf(kUpCutProc) == OpRole::LeaderOnly, "a close-up's choice is the subject player's");
    check(rule::isDoor(rule::op::kDoor) && rule::isDoor(rule::op::kUpCutDoor), "door ops");
    check(!rule::isDoor(rule::op::kMes), "not a door");
    check(!rule::followsAfter(rule::op::kTraceOff) && rule::followsAfter(rule::op::kTraceOn), "trace ops");
    check(rule::roleOf(rule::op::kWait) == OpRole::Wait, "wait");
    check(rule::roleOf(rule::op::kFadeInWait) == OpRole::Wait, "fade wait");
    constexpr uint16_t kFlagSet = 36;
    constexpr uint16_t kSetEnemyFlag = 39;
    constexpr uint16_t kSe = 79;
    constexpr uint16_t kCamChange = 135;
    constexpr uint16_t kDemoStart = 227;
    constexpr uint16_t kExit = 5;
    check(rule::roleOf(kFlagSet) == OpRole::Run, "story flags run on both");
    check(rule::roleOf(kSetEnemyFlag) == OpRole::Run, "enemy groups spawn on both");
    check(rule::roleOf(kSe) == OpRole::Run, "sounds play on both");
    check(rule::roleOf(kCamChange) == OpRole::Run, "camera cuts on both");
    check(rule::roleOf(kDemoStart) == OpRole::Run, "cutscenes on both");
    check(rule::roleOf(kExit) == OpRole::Run, "exit runs");
}

void testFollow() {
    check(rule::beforeOp(false, OpRole::Run) == Follow::WaitForLeader, "never ahead of the leader");
    check(rule::beforeOp(false, OpRole::Wait) == Follow::WaitForLeader, "not even a wait");
    check(rule::beforeOp(true, OpRole::Run) == Follow::Run, "a finished op runs here");
    check(rule::beforeOp(true, OpRole::LeaderOnly) == Follow::Skip, "a leader-only op is skipped");
    check(rule::beforeOp(true, OpRole::Wait) == Follow::Skip, "a wait the leader finished is skipped");
    check(rule::finished(0x31c, 0x320, rule::kResultNext), "moved on");
    check(rule::finished(0x31c, 0x31e, rule::kResultYield), "moved and yielded (frame)");
    check(rule::finished(0x31c, 0x31c, rule::kResultEnd), "exit");
    check(!rule::finished(0x31c, 0x31c, rule::kResultYield), "still waiting");
    check(rule::afterRun(true, 0) == rule::AfterRun::TakeLeadersBranch, "finished here: the leader's branch");
    check(rule::afterRun(false, rule::kStallMs - 1) == rule::AfterRun::KeepRunning, "still running: keep at it");
    check(rule::afterRun(false, rule::kStallMs) == rule::AfterRun::Force, "stalled: end it where the leader did");
}

void testLead() {
    check(rule::leadsHere(true, true), "the subject's owner leads");
    check(!rule::leadsHere(false, true), "the other machine follows once the peer has the thread");
    check(rule::leadsHere(false, false), "nobody follows: the machine that has it leads");
}

void testStartFate() {
    using rule::StartFate;
    check(rule::startFate(kRoom, kRoom, false, 0) == StartFate::Apply, "same room: follow");
    check(rule::startFate(kRoom, rule::kNoScene, false, 100) == StartFate::Wait, "still loading: wait");
    check(rule::startFate(kRoom, kOtherRoom, true, 100) == StartFate::Wait, "in a door: wait");
    check(rule::startFate(kRoom, kOtherRoom, false, 100) == StartFate::Drop, "another room: drop");
    check(rule::startFate(kRoom, rule::kNoScene, false, rule::kPendingStartMs) == StartFate::Drop, "too old: drop");
    check(rule::startFate(kRoom, kRoom, false, rule::kPendingStartMs * 2) == StartFate::Apply, "same room even late");
}

void testWire() {
    check(sizeof(event_sync::EventStart) == 16, "EVENT_START is 16 bytes");
    check(sizeof(event_sync::EventStep) == 8, "a step is 8 bytes");
}

}  // namespace

int main() {
    testClassify();
    testMayFire();
    testOpRoles();
    testFollow();
    testLead();
    testStartFate();
    testWire();
    if (g_failures == 0) std::printf("event_rule_test: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
