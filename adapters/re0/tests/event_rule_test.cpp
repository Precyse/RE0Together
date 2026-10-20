// Checks the room event replication rules (no game). Exit 0 when every check passes.
#include <cstdio>
#include <string>

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

void testBodyRoles() {
    namespace o = rule::op;
    const uint16_t bodyOps[] = {o::kSubMove, o::kCharPoint, o::kCharPointY, o::kCharAddY, o::kChPosSet, o::kCorrectPos,
                                o::kPlayerAnimSet, o::kPlayerAnimSet2, o::kPlayerAnimSetSpeed, o::kNeckRotation,
                                o::kPlayerAnimPause, o::kPlayerReset, o::kPlayerReset2, o::kPlayerClimb,
                                o::kPlayerClimbPos, o::kPlayerAddChild, o::kPlayerEventStart, o::kGameOver};
    for (const uint16_t code : bodyOps) check(rule::roleOf(code) == OpRole::Body, "a single-target body op is Body");
    constexpr uint16_t kDemoEntryPos = 226, kCorrectPos2 = 276, kAfterDemo = 233, kAfterDemo2 = 236, kAfterDemoNc = 270;
    constexpr uint16_t kPlayerPause = 158, kControlPause = 277;
    for (const uint16_t code : {kDemoEntryPos, kCorrectPos2, kAfterDemo, kAfterDemo2, kAfterDemoNc, kPlayerPause, kControlPause}) {
        check(rule::roleOf(code) == OpRole::Run, "a two-target op and the pause families keep Run");
    }
    check(rule::beforeOp(true, OpRole::Body) == Follow::Run, "a finished Body op runs on the follower");
}

void testSubjects() {
    namespace o = rule::op;
    check(rule::subjectOperand(o::kChPosSet) == 0 && rule::subjectOperand(o::kPlayerClimb) == 0, "the subject is operand 0");
    check(rule::subjectOperand(o::kCharPoint) == rule::kCharPointSubjectOperand, "CharPoint names it third");
    check(rule::subjectOperand(o::kSubMove) == rule::kNoOperand && rule::subjectOperand(o::kPlayerEventStart) == rule::kNoOperand,
          "SubMove and PlayerEventStart name no subject");
    check(rule::targetOf(o::kChPosSet, rule::kSubjectControlled) == rule::Target::Controlled, "0x20000 is the controlled one");
    check(rule::targetOf(o::kChPosSet, rule::kSubjectPartner) == rule::Target::Partner, "0x30000 is the partner");
    check(rule::targetOf(o::kChPosSet, 3) == rule::Target::Unit, "ChPosSet names units by ordinal");
    check(rule::targetOf(o::kCharAddY, 3) == rule::Target::Unit, "CharAddY names units by ordinal");
    check(rule::targetOf(o::kPlayerReset, 3) == rule::Target::Nobody, "Player_Reset ignores any other word");
    check(rule::targetOf(o::kSubMove, 0) == rule::Target::Partner, "SubMove moves the partner");
    check(rule::targetOf(o::kPlayerEventStart, 0) == rule::Target::Both, "PlayerEventStart reaches both");
}

void testBodyRun() {
    namespace o = rule::op;
    using rule::BodyRun;
    using rule::Puppets;
    const Puppets none{};
    const Puppets partnerPuppet{false, true, false};
    const Puppets controlledPuppet{true, false, false};
    const Puppets both{true, true, false};
    check(rule::runFor(o::kChPosSet, rule::kSubjectControlled, none, true) == BodyRun::Run, "owned characters run");
    check(rule::runFor(o::kChPosSet, rule::kSubjectPartner, partnerPuppet, true) == BodyRun::Skip, "a puppet partner skips");
    check(rule::runFor(o::kChPosSet, rule::kSubjectControlled, partnerPuppet, true) == BodyRun::Run, "the owned one still runs");
    check(rule::runFor(o::kChPosSet, rule::kSubjectControlled, controlledPuppet, true) == BodyRun::Skip,
          "a swapped perspective skips the puppet that became controlled");
    check(rule::runFor(o::kChPosSet, 3, Puppets{false, false, true}, true) == BodyRun::Skip, "a puppet unit skips");
    check(rule::runFor(o::kChPosSet, 3, partnerPuppet, true) == BodyRun::Run, "an ordinary unit runs");
    check(rule::runFor(o::kPlayerReset, 3, both, true) == BodyRun::Run, "an op that names no character runs");
    check(rule::runFor(o::kPlayerEventStart, 0, none, true) == BodyRun::Run, "both owned: PlayerEventStart runs");
    check(rule::runFor(o::kPlayerEventStart, 0, partnerPuppet, true) == BodyRun::RunLocalHalf,
          "PlayerEventStart runs on the owned half only");
    check(rule::runFor(o::kPlayerEventStart, 0, both, true) == BodyRun::Skip, "two puppets: nothing to start");
    check(rule::runFor(o::kSubMove, 0, partnerPuppet, true) == BodyRun::Skip, "SubMove skips when the owner runs the thread");
    check(rule::runFor(o::kSubMove, 0, partnerPuppet, false) == BodyRun::Run, "SubMove runs for an owner without the thread");
    check(rule::runFor(o::kChPosSet, rule::kSubjectPartner, partnerPuppet, false) == BodyRun::Skip,
          "only SubMove runs for an absent owner");
    check(rule::movesAbsentOwner(o::kSubMove, false) && !rule::movesAbsentOwner(o::kSubMove, true), "absent owner rule");
}

void testClimbPlacement() {
    namespace o = rule::op;
    check(rule::placesPartner(o::kPlayerClimb, rule::kClimbWithPartner), "climb type 7 places the partner");
    check(rule::placesPartner(o::kPlayerClimbPos, rule::kClimbWithPartner), "also with a position");
    check(!rule::placesPartner(o::kPlayerClimb, 1), "other climbs do not");
    check(!rule::placesPartner(o::kChPosSet, rule::kClimbWithPartner), "only climbs");
    check(rule::isClimb(o::kPlayerClimb) && !rule::isClimb(o::kPlayerReset), "climb ops");
}

void testObstacleTriggers() {
    constexpr uint32_t kBox = 1;
    check(rule::classify(4, kBox) == TriggerKind::Shared, "a box in a zone is shared in input mode");
    check(rule::classify(4, kBox, true) == TriggerKind::Obstacle, "a box in a zone is the pusher's in state mode");
    check(rule::classify(5, rule::kLastObstacleUnit, true) == TriggerKind::Obstacle, "the last obstacle unit too");
    check(rule::classify(5, rule::kLastObstacleUnit + 1, true) == TriggerKind::Shared, "past the table it is a plain unit");
    check(rule::classify(4, rule::kSubjectControlled, true) == TriggerKind::Local, "the controlled character stays local");
    check(rule::classify(4, kPartnerSubject, true) == TriggerKind::Shared, "the partner stays shared");
    check(rule::classify(14, kBox, true) == TriggerKind::Shared, "either character in a zone stays shared");
    check(rule::classify(11, kBox, true) == TriggerKind::Shared, "other conditions are untouched");
    check(rule::isObstacleUnit(0) && rule::isObstacleUnit(4) && !rule::isObstacleUnit(5), "obstacle unit words are 0..4");

    using Owner = rule::PropOwner;
    check(rule::mayFire(TriggerKind::Obstacle, true, false, Owner::Mine), "the pusher fires its box's trigger");
    check(rule::mayFire(TriggerKind::Obstacle, true, true, Owner::Mine), "also when it is not the room authority");
    check(!rule::mayFire(TriggerKind::Obstacle, true, true, Owner::Theirs), "the other machine does not, authority or not");
    check(rule::mayFire(TriggerKind::Obstacle, true, true, Owner::Unknown), "a box nobody pushed is the authority's");
    check(!rule::mayFire(TriggerKind::Obstacle, true, false, Owner::Unknown), "and not the other machine's");
    check(rule::mayFire(TriggerKind::Obstacle, false, false, Owner::Theirs), "a player alone in the room fires everything");
    check(rule::mayFire(TriggerKind::Shared, true, true, Owner::Theirs), "the owner only matters to obstacle triggers");
    check(std::string(rule::name(TriggerKind::Obstacle)) == "obstacle", "obstacle triggers are named in the log");
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
    testBodyRoles();
    testSubjects();
    testBodyRun();
    testClimbPlacement();
    testStartFate();
    testObstacleTriggers();
    testWire();
    if (g_failures == 0) std::printf("event_rule_test: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
