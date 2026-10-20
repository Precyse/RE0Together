#pragma once
#include <cstdint>

// Pure rules of room event replication (no game access, unit tested; docs/re/RE0_EVENT_SYNC.md). A room script thread
// runs on both machines. Its leader is the owner of the character the script treats as the player (its subject: the
// firer's character, until a CharChange op switches it to the other one); the leader runs it natively, the other
// machine follows: it runs each opcode only after the leader finished it and then takes the leader's branch.
namespace event_rule {

// Thread dispatch results (the op handler's return value).
constexpr uint8_t kResultYield = 0;
constexpr uint8_t kResultNext = 1;
constexpr uint8_t kResultEnd = 2;
constexpr uint8_t kResultKilled = 3;  // wire only: the leader's thread vanished without ending (room reset)

constexpr int64_t kStallMs = 3000;          // a followed op still running this long after the leader finished it ends
constexpr int64_t kPendingStartMs = 10000;  // a start for a room this machine is still loading waits this long

// Who may fire a trigger. Local conditions read only the controlled character (its zone, pad, inventory, the entry it
// came in by, the item it is using): each machine keeps its own character controlled, so they fire where that player
// met them. Shared conditions (flags, enemies, units, timers, either character) read state both machines share and
// fire on the room's authority only, or they would fire twice. In state mode a push block's zone trigger (conditions 4 and
// 5 naming an obstacle unit) is the pusher's: Obstacle fires on the machine that last pushed the box.
enum class TriggerKind : uint8_t { Local, Shared, Obstacle };

constexpr uint32_t kSubjectControlled = 0x20000;  // p2 of the "unit in zone" types 4 and 5: the controlled character
constexpr uint32_t kSubjectPartner = 0x30000;     // the partner (sPlayer +0x3c); any other word names a unit

constexpr uint32_t kLastObstacleUnit = 4;  // p2 0..4 name an obstacle unit through the engine's table at 0xcd5398

constexpr bool isObstacleUnit(uint32_t p2) { return p2 <= kLastObstacleUnit; }

// `obstaclesByPusher` is state mode: an obstacle unit's trigger belongs to its pusher.
constexpr TriggerKind classify(uint32_t type, uint32_t p2, bool obstaclesByPusher = false) {
    switch (type) {
        case 1: case 2: case 6: case 7: case 8: case 9: case 10: case 22: case 30: case 31:
            return TriggerKind::Local;
        case 4: case 5:
            if (p2 == kSubjectControlled) return TriggerKind::Local;
            return obstaclesByPusher && isObstacleUnit(p2) ? TriggerKind::Obstacle : TriggerKind::Shared;
        default:
            return TriggerKind::Shared;
    }
}

// Who pushed the box last, seen from this machine (prop_sync): the lease holder, else the last committer.
enum class PropOwner : uint8_t { Mine, Theirs, Unknown };

// An Obstacle trigger fires on the box's pusher; a box nobody pushed (Unknown) is the room authority's like any shared
// trigger. A player alone in a room fires everything.
constexpr bool mayFire(TriggerKind kind, bool peerInRoom, bool roomAuthority, PropOwner owner = PropOwner::Unknown) {
    if (kind == TriggerKind::Local || !peerInRoom) return true;
    if (kind == TriggerKind::Obstacle && owner != PropOwner::Unknown) return owner == PropOwner::Mine;
    return roomAuthority;
}

constexpr const char* name(TriggerKind kind) {
    switch (kind) {
        case TriggerKind::Local: return "local";
        case TriggerKind::Shared: return "shared";
        case TriggerKind::Obstacle: return "obstacle";
    }
    return "shared";
}

// Opcode indices (rows of the table at 0xcd57d8; tools/re0/bes2.py prints them by name).
namespace op {
constexpr uint16_t kFrame = 1, kWait = 2, kWaitMovie = 3;
constexpr uint16_t kMes = 49, kMesSel = 50, kMesCommon = 51, kMesSe = 52, kMesCommonSe = 53, kNomes = 54, kNomesEnd = 55;
constexpr uint16_t kDoor = 56, kDoorCamera = 57;
constexpr uint16_t kItemGet = 60, kItemCheck = 61, kItemGetCheck = 62, kItemSub = 63, kKeyCheck = 64;
constexpr uint16_t kSubItemJp = 65, kSubItemRet = 66;
constexpr uint16_t kAnimWaitType = 75, kSeWait = 80, kSavePoint = 81;
constexpr uint16_t kSubMove = 148, kCharPoint = 149, kCharPointY = 150, kCharAddY = 151, kChPosSet = 152;
constexpr uint16_t kPlayerClimb = 153, kPlayerClimbPos = 154, kPlayerEventStart = 155, kGameOver = 192;
constexpr uint16_t kPlayerAnimSet = 167, kPlayerAnimSet2 = 168, kPlayerReset = 169, kPlayerReset2 = 170;
constexpr uint16_t kPlayerAnimSetSpeed = 171, kNeckRotation = 173, kPlayerAddChild = 174;
constexpr uint16_t kCorrectPos = 209, kPlayerAnimPause = 231, kDemoStart = 227;
constexpr uint16_t kUpCutFirst = 86, kUpCutLast = 114;  // UpCutStart .. UpCutEffectEnd: the close-up screens
constexpr uint16_t kUpCutEndWait = 96, kUpCutDoor = 97, kUpCutDoorCamera = 98;
constexpr uint16_t kUpCutNoReload = 123, kUpCutSetKirari = 215, kUpCutEndDoff = 229, kUpCutEndUnreset = 234;
constexpr uint16_t kSub = 115, kSubReadyGet = 116, kSubReadyGetParam = 117, kSubReadySelect = 118, kSubReadyTemp = 119;
constexpr uint16_t kSubGetResult = 120, kSubGetItem = 121, kSubCall = 122, kSubItem = 124;
constexpr uint16_t kCharChange = 126, kCharChangeNc = 127, kCharChange2 = 128, kTraceOff = 132, kTraceOn = 133;
constexpr uint16_t kFadeInWait = 140, kFadeOutWait = 142, kEventProcWait = 147, kPlayerEquip = 157;
constexpr uint16_t kItemPut = 191, kDoorNoFadeOut = 205, kSubDelete = 206, kMesBlend = 213, kEventExec = 221;
constexpr uint16_t kMesEnd = 249, kWaitCancel = 250, kSubItemRet2 = 272, kMemoryWeapon = 281, kCompWeapon = 282;
constexpr uint16_t kDoorCancel = 299, kMesDialog = 302, kUseKey = 304;
}  // namespace op

// Who runs an opcode.
//   LeaderOnly: the subject's player's own screens and the state of that player's character another module carries:
//               messages and selections, close-ups, the inventory and item screens (inventory_sync,
//               floor_items_sync), doors (door_sync), the typewriter, equips (equip_refresh), and forks (each fork
//               arrives as its own start). The follower does not run it; its pc goes where the leader's went.
//   Wait:       only waits; the leader already waited, so the follower does not wait again.
//   Switch:     CharChange*: the script makes the other character the player. Run on neither machine (each player
//               keeps their own character); the subject changes and the thread's lead passes to its owner.
//   Follow:     TraceOff / TraceOn: the partner stops or starts following. Run on neither machine (party_mode owns the
//               game's follow flag): the thread notes it for its doors and the host sets the party mode.
//   Body:       the op writes one character's body (position, motion, think, attachment, reset): in state mode
//               it runs only where that character is owned (`bodyRun`); in input mode it is a Run.
//   Run:        everything else, on both machines, with the subject as the controlled character.
enum class OpRole : uint8_t { Run, LeaderOnly, Wait, Switch, Follow, Body };

constexpr OpRole roleOf(uint16_t opcode) {
    using namespace op;
    switch (opcode) {
        case kFrame: case kWait: case kWaitMovie: case kAnimWaitType: case kSeWait: case kUpCutEndWait:
        case kFadeInWait: case kFadeOutWait: case kEventProcWait: case kWaitCancel:
            return OpRole::Wait;
        case kCharChange: case kCharChangeNc: case kCharChange2:
            return OpRole::Switch;
        case kTraceOff: case kTraceOn:
            return OpRole::Follow;
        case kSubMove: case kCharPoint: case kCharPointY: case kCharAddY: case kChPosSet: case kCorrectPos:
        case kPlayerAnimSet: case kPlayerAnimSet2: case kPlayerAnimSetSpeed: case kNeckRotation:
        case kPlayerAnimPause: case kPlayerReset: case kPlayerReset2: case kPlayerClimb: case kPlayerClimbPos:
        case kPlayerAddChild: case kPlayerEventStart: case kGameOver:
            return OpRole::Body;
        case kUpCutNoReload: case kUpCutSetKirari: case kUpCutEndDoff: case kUpCutEndUnreset:
        case kMes: case kMesSel: case kMesCommon: case kMesSe: case kMesCommonSe: case kNomes: case kNomesEnd:
        case kMesBlend: case kMesEnd: case kMesDialog:
        case kDoor: case kDoorCamera: case kUpCutDoor: case kUpCutDoorCamera: case kDoorNoFadeOut: case kDoorCancel:
        case kItemGet: case kItemCheck: case kItemGetCheck: case kItemSub: case kKeyCheck: case kUseKey: case kItemPut:
        case kSubItemJp: case kSubItemRet: case kSubItemRet2: case kSubItem: case kSub: case kSubReadyGet:
        case kSubReadyGetParam: case kSubReadySelect: case kSubReadyTemp: case kSubGetResult: case kSubGetItem:
        case kSubCall: case kSubDelete:
        case kSavePoint: case kPlayerEquip: case kMemoryWeapon: case kCompWeapon: case kEventExec:
            return OpRole::LeaderOnly;
        default:
            return opcode >= kUpCutFirst && opcode <= kUpCutLast ? OpRole::LeaderOnly : OpRole::Run;
    }
}

// ---- Body ops (partner_mode=state, docs/RE0_NOTES.md "State mode: scripts and cutscenes") ----
// A Body op names the character it writes with a subject word (the first operand, the third for CharPoint): 0x20000 the
// controlled character, 0x30000 the partner, and for ChPosSet / CharAddY any other word an ordinal of the room's
// units. The word is resolved after the perspective swap, so the controlled character is the thread's subject. SubMove
// always moves the partner; PlayerEventStart starts the event mode of both characters. A character whose owner is the
// other machine is a puppet there: its body comes from the owner's stream, so the op must not write it.
enum class Target : uint8_t { Controlled, Partner, Unit, Both, Nobody };

constexpr int kNoOperand = -1;
constexpr int kCharPointSubjectOperand = 2;

// Index of the U4 operand holding the subject word, kNoOperand for the ops that name no subject.
constexpr int subjectOperand(uint16_t opcode) {
    switch (opcode) {
        case op::kSubMove: case op::kPlayerEventStart: return kNoOperand;
        case op::kCharPoint: return kCharPointSubjectOperand;
        default: return 0;
    }
}

constexpr bool namesUnits(uint16_t opcode) { return opcode == op::kChPosSet || opcode == op::kCharAddY; }

constexpr Target targetOf(uint16_t opcode, uint32_t word) {
    if (opcode == op::kSubMove) return Target::Partner;
    if (opcode == op::kPlayerEventStart) return Target::Both;
    if (word == kSubjectControlled) return Target::Controlled;
    if (word == kSubjectPartner) return Target::Partner;
    return namesUnits(opcode) ? Target::Unit : Target::Nobody;
}

// Which of the characters an op can reach are puppets on this machine.
struct Puppets {
    bool controlled = false;
    bool partner = false;
    bool unit = false;
};

// Run the op as it is, skip it (the owner runs it), or run it with the puppet's slot of sPlayer hidden so only the
// local character gets it (PlayerEventStart reaches both characters through sPlayer).
enum class BodyRun : uint8_t { Run, Skip, RunLocalHalf };

constexpr BodyRun bodyRun(Target target, Puppets puppets) {
    switch (target) {
        case Target::Controlled: return puppets.controlled ? BodyRun::Skip : BodyRun::Run;
        case Target::Partner: return puppets.partner ? BodyRun::Skip : BodyRun::Run;
        case Target::Unit: return puppets.unit ? BodyRun::Skip : BodyRun::Run;
        case Target::Both:
            if (puppets.controlled && puppets.partner) return BodyRun::Skip;
            return puppets.controlled || puppets.partner ? BodyRun::RunLocalHalf : BodyRun::Run;
        case Target::Nobody: break;
    }
    return BodyRun::Run;
}

// SubMove brings the partner into the loaded room; skipped for a puppet it would leave an owner that does not run this
// thread (it is in another room, or never got the start) where it is, so then it runs on the puppet and the result is
// handed to the owner (CHARACTER_PLACE).
constexpr bool movesAbsentOwner(uint16_t opcode, bool ownerRunsThread) {
    return opcode == op::kSubMove && !ownerRunsThread;
}

// What a thread does with the Body op at its pc: `word` is the op's subject word (ignored by SubMove and
// PlayerEventStart), `ownerRunsThread` says the puppet's owner has this thread.
constexpr BodyRun runFor(uint16_t opcode, uint32_t word, Puppets puppets, bool ownerRunsThread) {
    const BodyRun run = bodyRun(targetOf(opcode, word), puppets);
    return run == BodyRun::Skip && movesAbsentOwner(opcode, ownerRunsThread) ? BodyRun::Run : run;
}

// Climb type 7 (a step up beside the partner) places the partner on the first entry spot as a side effect, a write to
// the other character that its owner's own run of the op (skipped there as Body) never makes.
constexpr int32_t kClimbWithPartner = 7;

constexpr bool isClimb(uint16_t opcode) { return opcode == op::kPlayerClimb || opcode == op::kPlayerClimbPos; }

constexpr bool placesPartner(uint16_t opcode, int32_t climbType) {
    return isClimb(opcode) && climbType == kClimbWithPartner;
}

// Operand index of the climb type (S4) of PlayerClimb / PlayerClimb_Pos.
constexpr int kClimbTypeOperand = 1;

constexpr bool isDoor(uint16_t opcode) {
    using namespace op;
    return opcode == kDoor || opcode == kDoorCamera || opcode == kUpCutDoor || opcode == kUpCutDoorCamera ||
           opcode == kDoorNoFadeOut;
}

constexpr bool followsAfter(uint16_t traceOpcode) { return traceOpcode == op::kTraceOn; }

// An op finished when it moved the pc or ended the thread; a waiting op returns yield with the pc unchanged.
constexpr bool finished(uint32_t pcBefore, uint32_t pcAfter, uint8_t result) {
    return pcAfter != pcBefore || result == kResultEnd;
}

enum class Follow : uint8_t { WaitForLeader, Skip, Run };

// Before a follower's op: the leader has (not) finished the op at this pc yet. A Switch or Follow op is not run but
// handled by the caller (Skip).
constexpr Follow beforeOp(bool leaderFinished, OpRole role) {
    if (!leaderFinished) return Follow::WaitForLeader;
    return role == OpRole::Run || role == OpRole::Body ? Follow::Run : Follow::Skip;
}

enum class AfterRun : uint8_t { TakeLeadersBranch, KeepRunning, Force };

// After running a followed op here: finished here, or still running `sinceLeaderMs` after the leader finished it.
constexpr AfterRun afterRun(bool finishedHere, int64_t sinceLeaderMs) {
    if (finishedHere) return AfterRun::TakeLeadersBranch;
    return sinceLeaderMs >= kStallMs ? AfterRun::Force : AfterRun::KeepRunning;
}

// Who leads a thread whose subject is `subject`: this machine when it owns that character. Without a peer following
// the thread the machine that has it always leads.
constexpr bool leadsHere(bool subjectOwnedHere, bool peerFollows) { return subjectOwnedHere || !peerFollows; }

// A start for `startScene` arrived while this machine has `here` loaded (kNoScene while loading).
enum class StartFate : uint8_t { Apply, Wait, Drop };

constexpr uint16_t kNoScene = 0xffff;

constexpr StartFate startFate(uint16_t startScene, uint16_t here, bool doorRunning, int64_t ageMs) {
    if (here == startScene) return StartFate::Apply;
    if (ageMs >= kPendingStartMs) return StartFate::Drop;
    return here == kNoScene || doorRunning ? StartFate::Wait : StartFate::Drop;
}

}  // namespace event_rule
