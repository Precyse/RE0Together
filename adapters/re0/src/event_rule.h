#pragma once
#include <cstdint>

// Pure rules of room event replication (no game access, unit tested; docs/re/RE0_EVENT_SYNC.md). A room script thread
// is fired on one machine (the firer); the other machine runs the same thread as a follower that runs each opcode only
// after the firer finished it and then takes the firer's branch.
namespace event_rule {

// Thread dispatch results (the op handler's return value).
constexpr uint8_t kResultYield = 0;
constexpr uint8_t kResultNext = 1;
constexpr uint8_t kResultEnd = 2;
constexpr uint8_t kResultKilled = 3;  // wire only: the firer's thread vanished without ending (room reset)

constexpr int64_t kStallMs = 3000;          // a followed op still running this long after the firer finished it ends
constexpr int64_t kPendingStartMs = 10000;  // a start for a room this machine is still loading waits this long

// Who may fire a trigger. Local conditions read only the controlled character (its zone, pad, inventory, the entry it
// came in by, the item it is using): each machine keeps its own character controlled, so they fire where that player
// met them. Shared conditions (flags, enemies, units, timers, either character) read state both machines share and
// fire on the room's authority only, or they would fire twice.
enum class TriggerKind : uint8_t { Local, Shared };

constexpr uint32_t kSubjectControlled = 0x20000;  // p2 of the "unit in zone" types 4 and 5: the controlled character

constexpr TriggerKind classify(uint32_t type, uint32_t p2) {
    switch (type) {
        case 1: case 2: case 6: case 7: case 8: case 9: case 10: case 22: case 30: case 31:
            return TriggerKind::Local;
        case 4: case 5:
            return p2 == kSubjectControlled ? TriggerKind::Local : TriggerKind::Shared;
        default:
            return TriggerKind::Shared;
    }
}

constexpr bool mayFire(TriggerKind kind, bool peerInRoom, bool roomAuthority) {
    return kind == TriggerKind::Local || !peerInRoom || roomAuthority;
}

constexpr const char* name(TriggerKind kind) { return kind == TriggerKind::Local ? "local" : "shared"; }

// Opcode indices (rows of the table at 0xcd57d8; tools/re0/bes2.py prints them by name).
namespace op {
constexpr uint16_t kFrame = 1, kWait = 2, kWaitMovie = 3;
constexpr uint16_t kMes = 49, kMesSel = 50, kMesCommon = 51, kMesSe = 52, kMesCommonSe = 53, kNomes = 54, kNomesEnd = 55;
constexpr uint16_t kDoor = 56, kDoorCamera = 57;
constexpr uint16_t kItemGet = 60, kItemCheck = 61, kItemGetCheck = 62, kItemSub = 63, kKeyCheck = 64;
constexpr uint16_t kSubItemJp = 65, kSubItemRet = 66;
constexpr uint16_t kAnimWaitType = 75, kSeWait = 80, kSavePoint = 81;
constexpr uint16_t kUpCutEndWait = 96, kUpCutDoor = 97, kUpCutDoorCamera = 98;
constexpr uint16_t kSub = 115, kSubReadyGet = 116, kSubReadyGetParam = 117, kSubReadySelect = 118, kSubReadyTemp = 119;
constexpr uint16_t kSubGetResult = 120, kSubGetItem = 121, kSubCall = 122, kSubItem = 124;
constexpr uint16_t kCharChange = 126, kCharChangeNc = 127, kCharChange2 = 128, kTraceOff = 132, kTraceOn = 133;
constexpr uint16_t kFadeInWait = 140, kFadeOutWait = 142, kEventProcWait = 147, kPlayerEquip = 157;
constexpr uint16_t kItemPut = 191, kDoorNoFadeOut = 205, kSubDelete = 206, kMesBlend = 213, kEventExec = 221;
constexpr uint16_t kMesEnd = 249, kWaitCancel = 250, kSubItemRet2 = 272, kMemoryWeapon = 281, kCompWeapon = 282;
constexpr uint16_t kDoorCancel = 299, kMesDialog = 302, kUseKey = 304;
}  // namespace op

// What a follower does with an opcode the firer finished.
//   FirerOnly: the firer's own screens and the state another module owns: messages and selections, the inventory and
//              item screens (inventory_sync, floor_items_sync), doors (door_sync), the typewriter, the character
//              switch and follow toggles (camera_parity, party_mode), equips (equip_refresh), and forks (each fork
//              arrives as its own start). Not run; the pc goes where the firer's went.
//   Wait:      only waits; the firer already waited, so the follower does not wait again.
//   Run:       everything else, with the firer's character as the controlled one.
enum class OpRole : uint8_t { Run, FirerOnly, Wait };

constexpr OpRole roleOf(uint16_t opcode) {
    using namespace op;
    switch (opcode) {
        case kFrame: case kWait: case kWaitMovie: case kAnimWaitType: case kSeWait: case kUpCutEndWait:
        case kFadeInWait: case kFadeOutWait: case kEventProcWait: case kWaitCancel:
            return OpRole::Wait;
        case kMes: case kMesSel: case kMesCommon: case kMesSe: case kMesCommonSe: case kNomes: case kNomesEnd:
        case kMesBlend: case kMesEnd: case kMesDialog:
        case kDoor: case kDoorCamera: case kUpCutDoor: case kUpCutDoorCamera: case kDoorNoFadeOut: case kDoorCancel:
        case kItemGet: case kItemCheck: case kItemGetCheck: case kItemSub: case kKeyCheck: case kUseKey: case kItemPut:
        case kSubItemJp: case kSubItemRet: case kSubItemRet2: case kSubItem: case kSub: case kSubReadyGet:
        case kSubReadyGetParam: case kSubReadySelect: case kSubReadyTemp: case kSubGetResult: case kSubGetItem:
        case kSubCall: case kSubDelete:
        case kSavePoint: case kCharChange: case kCharChangeNc: case kCharChange2: case kTraceOff: case kTraceOn:
        case kPlayerEquip: case kMemoryWeapon: case kCompWeapon: case kEventExec:
            return OpRole::FirerOnly;
        default:
            return OpRole::Run;
    }
}

// An op finished when it moved the pc or ended the thread; a waiting op returns yield with the pc unchanged.
constexpr bool finished(uint32_t pcBefore, uint32_t pcAfter, uint8_t result) {
    return pcAfter != pcBefore || result == kResultEnd;
}

enum class Follow : uint8_t { WaitForFirer, Skip, Run };

// Before a follower's op: the firer has (not) finished the op at this pc yet.
constexpr Follow beforeOp(bool firerFinished, OpRole role) {
    if (!firerFinished) return Follow::WaitForFirer;
    return role == OpRole::Run ? Follow::Run : Follow::Skip;
}

enum class AfterRun : uint8_t { TakeFirersBranch, KeepRunning, Force };

// After running a followed op here: finished here, or still running `sinceFirerMs` after the firer finished it.
constexpr AfterRun afterRun(bool finishedHere, int64_t sinceFirerMs) {
    if (finishedHere) return AfterRun::TakeFirersBranch;
    return sinceFirerMs >= kStallMs ? AfterRun::Force : AfterRun::KeepRunning;
}

// A start for `startScene` arrived while this machine has `here` loaded (kNoScene while loading).
enum class StartFate : uint8_t { Apply, Wait, Drop };

constexpr uint16_t kNoScene = 0xffff;

constexpr StartFate startFate(uint16_t startScene, uint16_t here, bool doorRunning, int64_t ageMs) {
    if (here == startScene) return StartFate::Apply;
    if (ageMs >= kPendingStartMs) return StartFate::Drop;
    return here == kNoScene || doorRunning ? StartFate::Wait : StartFate::Drop;
}

}  // namespace event_rule
