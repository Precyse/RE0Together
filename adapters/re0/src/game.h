#pragma once
// RE0 HD (Steam build 17178773, re0hd.exe, no ASLR). Addresses and offsets from docs/RE0_NOTES.md.
#include <windows.h>

#include <array>
#include <cstdint>
#include <cstring>

namespace game {

// Singleton pointer globals
constexpr uintptr_t kPlayerGlobal = 0xdcbf3c;      // sPlayer*
constexpr uintptr_t kGameCharaGlobal = 0xdcc0d0;   // sGameChara*
constexpr uintptr_t kGamePadGlobal = 0xe2d7c8;     // sGamePad*
constexpr uintptr_t kGameInfoGlobal = 0xdcbe9c;    // sGameInfo*
constexpr uintptr_t kDoorLoadGlobal = 0xdcbeb8;    // sDoorLoad*
constexpr uintptr_t kSubMenuGlobal = 0xdcebd0;     // sSubMenu*
constexpr uintptr_t kItemGlobal = 0xdcbf44;        // sItem*
constexpr uintptr_t kFlagManagerGlobal = 0xdcc014; // sFlagManager* (size 0x144, vtable 0xcd9184)

// sFlagManager: story flags as a bitset of flag_diff::kWords dwords (0x11c bytes; set(index, count, value) is 0x59c980,
// the save copy 0x59ba30 copies the same 0x47 dwords)
constexpr uintptr_t kFlagBitsOffset = 0x20;
// thiscall(sFlagManager, index, value): the game's own bit write plus per-family side effects (enemy, effect, display)
constexpr uintptr_t kFlagSetFunction = 0x59bd50;
// fastcall(object) with the object read from kBgmRefreshObjectGlobal: redraws the BGM after a BGM flag changes
constexpr uintptr_t kBgmRefreshObjectGlobal = 0xdd20a0;
constexpr uintptr_t kBgmRefreshFunction = 0x643490;

// sPlayer
constexpr uintptr_t kPlayerControlledOffset = 0x2c;
constexpr uintptr_t kPlayerPartnerOffset = 0x3c;
constexpr uintptr_t kPlayerFollowOffset = 0x40;  // u8: 1 while the partner follows (the game's own E toggles it)

// sGameChara mirrors the sPlayer pair
constexpr uintptr_t kGameCharaControlledOffset = 0x148c;
constexpr uintptr_t kGameCharaPartnerOffset = 0x149c;

// uPlayerBase (uPlayerBilly / uPlayerRebecca)
constexpr size_t kPlayerBaseSize = 0x6AD0;
constexpr uintptr_t kPlayerPositionOffset = 0x40;     // vec3 + pad
constexpr uintptr_t kPlayerRotationOffset = 0x50;     // quaternion xyzw
constexpr uintptr_t kPlayerScaleOffset = 0x60;        // vec3
constexpr uintptr_t kPlayerWorldMatrixOffset = 0x70;  // 4x4, current
constexpr uintptr_t kPlayerPrevWorldMatrixOffset = 0xB0;
constexpr uintptr_t kPlayerThinkOffset = 0x67b8;
constexpr uintptr_t kThinkSetterFunction = 0x508b1e;

// Character classes, told apart by the object's vptr (the tracer patches vtable entries, not the vptr)
constexpr uintptr_t kBillyVtable = 0xccf5a0;     // uPlayerBilly
constexpr uintptr_t kRebeccaVtable = 0xccfd10;   // uPlayerRebecca

constexpr uintptr_t kPlayerHpOffset = 0x1030;  // i32
constexpr uintptr_t kPlayerFlagsOffset = 0xc;  // u32 attribute flags
constexpr uint32_t kPlayerInCurrentRoomFlag = 0x4000;

// sItem: one fixed inventory block per character (6 slots of {u32 itemId, u32 count} plus 0x10 of extras)
constexpr uintptr_t kItemRebeccaBlockOffset = 0x24;
constexpr uintptr_t kItemBillyBlockOffset = 0x64;
constexpr size_t kInventoryBlockSize = 0x40;

// The game's equip step after a menu closes (0x5d7740, for both characters): the block returns the weapon type of its
// equipped slot, the player switches to it (creates the weapon unit, sets the weapon type +0x6950), then refreshes.
constexpr uintptr_t kPlayerItemBlockFunction = 0x50dc70;   // thiscall(player) -> block pointer (equipped slot at +0x3c)
constexpr uintptr_t kBlockSetEquippedFunction = 0x4db2f0;  // thiscall(block, slot) ret 4 -> weapon type or -1
constexpr uintptr_t kPlayerSetWeaponTypeFunction = 0x50d870;  // thiscall(player, type) ret 4
constexpr uintptr_t kPlayerWeaponAttachFunction = 0x50d670;   // thiscall(player)
constexpr uintptr_t kPlayerWeaponAimFunction = 0x516630;      // thiscall(player)
constexpr uintptr_t kBlockEquippedOffset = 0x3c;              // u32 equipped slot in the block the player function returns
constexpr uintptr_t kPlayerWeaponTypeOffset = 0x6950;         // i32 held weapon type (0x50f740 reads it)

// Weapon resource sets (the weapon's arc: model and the player's motion lists for it) in sGameChara, keyed by unit
// kind and weapon type. Before the equip step the menu close (0x5d8040) releases the set of the weapon a character
// leaves, requests the one it takes with the character's room record (+0xff4), and waits until every request loaded;
// setWeaponType only looks the motion lists up (sResource flag 0x80), so equipping first leaves the bind pose.
constexpr int32_t kWeaponUnitKind = 0x74;
constexpr uintptr_t kReleaseSetFunction = 0x520850;     // thiscall(sGameChara, kind, type) ret 8
constexpr uintptr_t kRequestSetFunction = 0x5203d0;     // thiscall(sGameChara, kind, type, record) ret 0xc
constexpr uintptr_t kRequestsLoadedFunction = 0x520240; // thiscall(sGameChara) -> bool; each call drives the loads

// Outfits (docs/RE0_NOTES.md "Outfits"). sPlayerCostume holds each family's outfit list and current outfit; the model
// byte of an outfit is part of the player's resource key, so a change releases the old set and requests the new one
// (the menu close 0x5d8040 does exactly that) before sPlayer rebinds the player's resource.
constexpr uintptr_t kPlayerCostumeGlobal = 0xdcc00c;       // sPlayerCostume*
constexpr uintptr_t kCurrentCostumeFunction = 0x546b70;    // thiscall(kind) ret 4 -> u8 current model
constexpr uintptr_t kFindCostumeFunction = 0x546940;       // thiscall(kind, model) ret 8 -> entry* (first byte = model), the current entry when absent
constexpr uintptr_t kSetCostumeFunction = 0x547130;        // thiscall(kind, model) ret 8 -> bool; family list only, also writes the save record
constexpr uintptr_t kGameStartFunction = 0x405140;         // thiscall(game start), no stack arguments; stores the starting outfits
constexpr uintptr_t kGameStartSoundFunction = 0x59df90;    // thiscall, no stack arguments; selects the voice bank variant (automatic)
constexpr uintptr_t kPlayerRebuildFunction = 0x4ebcc0;     // thiscall(sPlayer, index) ret 4: binds the newly loaded resource to a player
constexpr uintptr_t kResourceFailedFunction = 0x5200e0;    // thiscall(sGameChara, int* kind) -> bool: a requested resource failed to load
constexpr uintptr_t kSoundGlobal = 0xdcc070;               // sSound*
constexpr uintptr_t kVoiceVariantFunction = 0x64a560;      // thiscall(sSound, variant) ret 4: reloads voice bank 10
constexpr uintptr_t kLanguageGlobal = 0xdcc06c;            // object whose 0x544f70 returns the language
constexpr uintptr_t kLanguageFunction = 0x544f70;          // thiscall -> language
constexpr int kFirstRebeccaCostumeKind = 1;                // kinds 1..3 are Rebecca's family, any other kind is Billy's
constexpr int kLastRebeccaCostumeKind = 3;
constexpr int kRebeccaCostumeKind = 2;
constexpr int kBillyCostumeKind = 5;
constexpr int kRebeccaPlayerIndex = 0;
constexpr int kBillyPlayerIndex = 1;
constexpr int kVoiceWeskerVariant = 2;                     // voice_2 (English); the next variant is voice_2_jp
constexpr int kLanguageEnglish = 1;

// sItemPut: the dropped floor items, 28 records; a live record points at its spawned uItem
constexpr uintptr_t kItemPutGlobal = 0xdce0a8;  // sItemPut*
constexpr uintptr_t kItemPutRecordsOffset = 0x20;
constexpr size_t kItemPutRecordCount = 28;
constexpr int32_t kItemPutEmptyState = -1;
constexpr uintptr_t kUnitPositionOffset = 0x40;  // vec3, every MT unit (uItem included)
constexpr uintptr_t kItemPutFunction = 0x4de500;     // thiscall on sItemPut, (ItemDesc*, position*, euler rotation*), ret 0xC, returns the uItem
// The room script's item placement loop (a script opcode at 0x581a50..0x581b7c) calls put at 0x581b53 for every map item
// each time the room loads; the only other caller is the player's drop at 0x5e4b84. Return address of the script call:
constexpr uintptr_t kScriptItemPutReturn = 0x581b58;
constexpr uintptr_t kItemRemoveFunction = 0x4de730;  // thiscall, (uItem*), ret 4; loads its own `this` from [0xdcbf40], ecx is ignored

// Pickup action step (reached through the handler table at 0xd8ac6c): thiscall on the action state, (player), ret 4.
// Phase 1 waits for the animation, looks the item up by key without a null check and takes it.
constexpr uintptr_t kPickupStepFunction = 0x500d00;
constexpr uintptr_t kPickupPhaseOffset = 0x10;  // u32 phase of the action state: 0 start, 1 take, 2+ done
constexpr uintptr_t kPickupPlayerOffset = 0x18;  // the player doing the pickup
// Player field: the interaction target (the item being picked up); the game clears it when that item is removed.
constexpr uintptr_t kPlayerInteractTargetOffset = 0x67a0;
constexpr uint32_t kPickupStartPhase = 0;
constexpr uint32_t kPickupTakePhase = 1;
constexpr uint32_t kPickupDonePhase = 2;

struct ItemDesc {
    uint32_t itemId;
    uint32_t count;
    uint32_t reserved;
};
static_assert(sizeof(ItemDesc) == 12);

struct ItemPutRecord {
    uint32_t key;
    uint32_t reserved0;
    uint32_t itemId;
    uint32_t count;
    uint32_t item;  // uItem*
    int32_t state;  // kItemPutEmptyState when the record is free
    uint32_t reserved1[3];
};
static_assert(sizeof(ItemPutRecord) == 0x24);

// state mode: phase 12 (item ledger; docs/RE0_NOTES.md "State mode: item ledger")
// The item data record the player's interaction target points at during a pickup: the take step reads the item id at +4
// and the stack count at +8 (u16) from it (0x4ded40 returns the record + 4).
constexpr uintptr_t kItemRecordIdOffset = 0x4;
constexpr uintptr_t kItemRecordCountOffset = 0x8;

// state mode: phase 8 (enemy hand-over and creation on demand; docs/RE0_NOTES.md "State mode: enemy hits and hand-over")
// The create 0x410bb0 is thiscall on the spawn table (ECX, kept from the caller) with (room record, spawn record index,
// flags) and ret 0xc: the room load (0x61b240 -> 0x4109d0) and the flag setter's spawns (0x4109f0) pass the room's
// record and read the flags from the global below. The caller then moves the new enemy by the record's origin and
// assigns it to the record (kSceneAssignFunction).
constexpr uintptr_t kEnemyCreateFlagsGlobal = 0xdcf2e4;  // u32, the create's third argument
constexpr uintptr_t kSceneRecordOriginOffset = 0x20;     // vec3 added to a created enemy's position
constexpr uintptr_t kUnitPositionWOffset = 0x4c;         // u32, cleared after the origin is added

// Rooms, doors and menus (single-byte reads are enough for every value below except the door phase, an i32)
constexpr uintptr_t kGameInfoStageOffset = 0x2a80;
constexpr uintptr_t kGameInfoRoomOffset = 0x2a84;
constexpr uintptr_t kDoorLoadStateOffset = 0x44;
constexpr uintptr_t kDoorLoadRoomOffset = 0x20;  // u32 room (scene id) the running door leads to, stored by start
// sDoorLoad's last phase (5, from update 0x552300) finishes the door, which ends the room phase DoorLoad and starts the
// room, once its fade timer ran out, the fade is done and this check (thiscall, no arguments, bool: the door model's
// motion finished) passes; 0x552629 is its only caller.
constexpr uintptr_t kDoorFinishCheckFunction = 0x551c70;
constexpr uintptr_t kRoomControlGlobal = 0xdcbeb4;         // sRoomControl*
constexpr uintptr_t kRoomPhaseCurrentOffset = 0xb8 + 0x14;  // phase manager +0x14: room_phase::Phase
constexpr uintptr_t kRoomPhaseRequestedOffset = 0xb8 + 0x18;  // phase manager +0x18: the phase asked for, kNoRoomPhase when none
constexpr uintptr_t kRoomPhaseDepthOffset = 0xb8 + 0x3c;      // phase manager +0x3c: depth of the stack of pushed phases
constexpr uintptr_t kRoomPhaseStackOffset = 0xb8 + 0x1c;      // phase manager +0x1c: i32[kRoomPhaseStackSize], push 0x60a340 stores the phase it covers at [depth]
constexpr int32_t kRoomPhaseStackSize = 8;                    // up to +0x3c, the depth
constexpr int32_t kNoRoomPhase = -1;
// sRoomControl::popPhase: thiscall, no arguments; `add ecx,0xb8; jmp` to the phase manager's pop (`dec [ecx+0x3c]`, then
// requested = stack[depth]), which has no depth guard: a pop at depth 0 reads below the stack. The script op EventPhaseOut
// 0x56cd10 ends an event with it (after clearing sGameInfo +0x50 bits kPlayerPauseBit and kEventPhaseBit).
constexpr uintptr_t kRoomPopPhaseFunction = 0x610df0;
constexpr std::array<uint8_t, 7> kRoomPopPhaseBytes = {0x81, 0xc1, 0xb8, 0x00, 0x00, 0x00, 0xe9};
constexpr uintptr_t kPhaseManagerPopFunction = 0x60a330;
constexpr std::array<uint8_t, 14> kPhaseManagerPopBytes = {0xff, 0x49, 0x3c, 0x8b, 0x41, 0x3c, 0x8b,
                                                           0x44, 0x81, 0x1c, 0x89, 0x41, 0x18, 0xc3};
// sRoomControl::requestPhase: thiscall (phase), ret 4. The vanilla V requests Change (9), which zaps to the partner
// and loads its room when the two are apart (player think 0x4fed48 / 0x50395e).
constexpr uintptr_t kRequestRoomPhaseFunction = 0x610e00;
// sDoorLoad::start: thiscall (room, entry, a, b, flag), ret 0x14. Every room change starts here: it plays the door,
// counts down and then calls sRoomControl::changeRoom (0x610c60), which carries the partner when sPlayer +0x40 is set
// and partner +0xff4 equals the scene's current room record.
constexpr uintptr_t kDoorStartFunction = 0x552b50;
// Event-script condition "the controlled player acts on this trigger": cdecl bool(void*, context*), true when the
// controlled player's trigger zone (player +0x16e4, also kept for the partner) equals context +8, pad 0 action is
// pressed and the player may act. Doors and other in-room interactions go through it.
constexpr uintptr_t kActOnTriggerFunction = 0x564070;
constexpr uintptr_t kSubMenuStateOffset = 0x2c;
// Save manager (singleton 0xdcc018, +0x20 = request object, dispatch 0x6133d0 on op +4): requests are thiscall
// on the request object and return false while another request runs. Save (slot, 0) ret 8 is op 3 (0x6134c0) or
// op 4, save by copy (0x613500); load (slot) ret 4 is op 5 (0x613390). The save screen and the options menu also
// save the system data (slot 0x15) through the save requests; the load menu passes its cursor index as the slot.
constexpr uintptr_t kSaveRequestFunction = 0x6134c0;
constexpr uintptr_t kSaveCopyRequestFunction = 0x613500;
constexpr uintptr_t kLoadRequestFunction = 0x613390;
constexpr uintptr_t kSubMenuOpenFunction = 0x5d9030;  // thiscall, no args: opens the inventory for the focused character
constexpr uint8_t kSubMenuClosed = 0x0d;
// cPlayerThink vtable slot 28, reached from the lethal hit: kills the player (setHP 0) and enters the dead state.
// Save data owner check: compares the SteamID stamped in the save (+0xa0) with SteamUser()->GetSteamID().
constexpr uintptr_t kSaveOwnerCheckFunction = 0x612600;  // thiscall, no args, returns bool
constexpr uintptr_t kPlayerOnDeathFunction = 0x4fcea0;  // thiscall on the think, 1 stack arg (player), ret 4

// Scenes (static analysis of the door carry 0x61e2c0, 2026-09-30). sSceneInfo keeps up to 4 room records ("scenes",
// pointers at +0x2dc70); the loaded room is the current record (+0x20), a room a character was left in stays as a
// dormant record. A character belongs to the record at +0xff4. Record +8 = scene id, the room argument doors pass
// (0x24, 0x2f...); +0x9aa4 = entry positions, 0x18 bytes per (entry * 3 + mode).
constexpr uintptr_t kSceneInfoGlobal = 0xdcbf40;           // sSceneInfo*
constexpr uintptr_t kSceneCurrentRecordOffset = 0x20;
constexpr uintptr_t kSceneRecordIdOffset = 8;
constexpr uintptr_t kPlayerSceneRecordOffset = 0xff4;
constexpr uintptr_t kPlayerUnitHandleOffset = 0xff0;       // handle in the per-room unit registry
// thiscall (scene id, flags) ret 8: the record of a room, loading a dormant one when no record holds it.
constexpr uintptr_t kSceneRecordFunction = 0x61e0f0;
constexpr uintptr_t kSceneDefaultFlagsGlobal = 0xdd1d44;   // u32 flags a door passes, plus kSceneDoorFlag
constexpr uint32_t kSceneDoorFlag = 4;
// thiscall on a record (player, entry, mode) ret 0xC: puts the player on the entry's spot (mode 0 the one coming
// through the door, 2 a following partner) and commits its transform.
constexpr uintptr_t kScenePlaceFunction = 0x61ed50;
constexpr uint32_t kScenePlaceDoorMode = 0;
constexpr uint32_t kScenePlaceSideMode = 1;    // read live: a second spot beside the door spot (+50 on one axis)
constexpr uint32_t kScenePlaceFollowMode = 2;  // read live: a spot behind it (-50), the same as mode 0 at some entries
constexpr uint32_t kScenePlaceModeCount = 3;
constexpr uintptr_t kSceneEntrySpotsOffset = 0x9aa4;
constexpr uintptr_t kSceneEntrySpotSize = 0x18;  // {f32 x, y, z, then 3 f32 of facing}
// thiscall on sSceneInfo (record, player) ret 8: moves the player into the record (leave/enter callbacks included).
constexpr uintptr_t kSceneAssignFunction = 0x619e30;

// Room event scripts (docs/re/RE0_EVENT_SYNC.md). sEventScript runs the loaded room's script (nativePC/event/*.bes2):
// 8 thread slots of 0x88 bytes, each running big-endian u16 opcodes from the table at 0xcd57d8.
constexpr uintptr_t kEventScriptGlobal = 0xdcbebc;  // sEventScript*
constexpr uintptr_t kScriptThreadsOffset = 0x114;
constexpr uintptr_t kScriptThreadSize = 0x88;
constexpr size_t kScriptThreadCount = 8;
constexpr uintptr_t kThreadActiveOffset = 0;        // u8, 1 while the slot runs
constexpr uintptr_t kThreadKeyOffset = 4;           // u32 script key (the scene id for room triggers)
constexpr uintptr_t kThreadCodeOffset = 8;          // code base (offsets in the file are from it)
constexpr uintptr_t kThreadPcOffset = 0xc;          // u32 offset of the next opcode
constexpr uintptr_t kThreadIndexOffset = 0x34;      // u16 trigger index, kForkThreadIndex for a forked thread
constexpr uintptr_t kThreadSubStateOffset = 0x38;   // u32 progress of the current opcode, 0 when it starts
constexpr uint16_t kForkThreadIndex = 0xffff;
// thiscall (key, trigger index, condition type) ret 0xc: starts the trigger's thread, or returns 0 when one of
// (key, index) already runs or the script is not loaded.
constexpr uintptr_t kStartTriggerThreadFunction = 0x57f800;
constexpr uintptr_t kStartForkThreadFunction = 0x57f7e0;      // thiscall (key, pc) ret 8, EventExec's start
constexpr uintptr_t kFindTriggerThreadFunction = 0x57f840;    // thiscall (key, index) ret 8 -> running thread or 0
constexpr uintptr_t kTriggerEntryFunction = 0x57fd70;         // thiscall (key, index) ret 8 -> {type, code, p1, p2}
constexpr uintptr_t kTriggerEntryParam2Offset = 0xc;
// thiscall on the interpreter (thread) ret 4: runs the thread's next opcode; 1 = run the next one now, 0 = yield to the
// next frame, 2 = the thread ends.
constexpr uintptr_t kScriptDispatchFunction = 0x57ce80;
constexpr uintptr_t kEventScriptUpdateFunction = 0x5835a0;    // thiscall, no args: runs every thread once per frame
// Opcode table rows {handler, signature, name}; the operand bytes of a signature come from a cdecl sizer.
constexpr uintptr_t kOpcodeTable = 0xcd57d8;
constexpr uintptr_t kOpcodeRowSize = 12;
constexpr uintptr_t kOpcodeSignatureOffset = 4;
constexpr uintptr_t kOperandSizeFunction = 0x57cc10;  // cdecl int(const char* signature)
constexpr uint32_t kOpcodeSize = 2;                   // the big-endian opcode itself
// Return addresses of the startThreadOnce callers: the Main phase's trigger scan 0x568270, the inventory's item use
// 0x568080, and its "can this item be used here" probe 0x567e90 (runs the thread's first ops, then ends it).
constexpr uintptr_t kTriggerScanStartReturn = 0x568530;
constexpr uintptr_t kItemUseStartReturn = 0x568193;
constexpr uintptr_t kItemProbeStartReturn = 0x567fa1;
// thiscall on sSceneInfo (record) ret 4: unloads a record nobody is in.
constexpr uintptr_t kSceneReleaseFunction = 0x61dde0;
// thiscall on [0xdcc010] (scene id or -1, unit handle) ret 8: the per-room unit registry the carry updates.
constexpr uintptr_t kUnitRoomRegistryGlobal = 0xdcc010;
constexpr uintptr_t kUnitRoomRegistryFunction = 0x411570;
constexpr int32_t kNoScene = -1;

// state mode: phase 11 (script body ops and the shared demo skip; docs/RE0_NOTES.md "State mode: scripts and cutscenes")
constexpr uintptr_t kOpcodeNameOffset = 8;            // char* in an opcode row
constexpr uintptr_t kUnitOrdinalFunction = 0x40ff00;  // thiscall on [kUnitRoomRegistryGlobal] (script key, ordinal) ret 8 -> unit index
constexpr uint32_t kNoUnit = 0xffffffff;
constexpr uintptr_t kUnitObjectFunction = 0x51fed0;   // thiscall on sGameChara (unit index) ret 4 -> object or 0
// uEventDemoScheduler update (fastcall, this in ECX): state at +0x58, 5 plays the demo and tests for a skip, 6 fades out.
constexpr uintptr_t kDemoSchedulerUpdateFunction = 0x54ac30;
constexpr uintptr_t kDemoStateOffset = 0x58;
constexpr uint32_t kDemoStatePlaying = 5;
constexpr uint32_t kDemoStateFadingOut = 6;
// The skip branch of state 5 starts the fade-out with the duration at +0x64 of the object in [0xdcbe98], on the object
// at +0x20 of [0xdcbea4] (thiscall (float duration) ret 4), then sets state 6.
constexpr uintptr_t kDemoFadeDurationGlobal = 0xdcbe98;
constexpr uintptr_t kDemoFadeDurationOffset = 0x64;
constexpr uintptr_t kDemoFadeHolderGlobal = 0xdcbea4;
constexpr uintptr_t kDemoFadeObjectOffset = 0x20;
constexpr uintptr_t kFadeStartFunction = 0x5aa410;
// The inputs that test true for a skip in state 5 (0x54b496 .. 0x54b4f9): the active pad's trigger word bit 8, the
// keyboard word & 0x8050000, the mouse byte & 2. The pad word is [pad + [pad + 0xc38] * 0x2cc + 0x1a0].
constexpr uintptr_t kPadActiveIndexOffset = 0xc38;
constexpr uintptr_t kPadDeviceStride = 0x2cc;
constexpr uintptr_t kPadTriggerWordOffset = 0x1a0;
constexpr uint32_t kPadSkipBit = 8;
constexpr uintptr_t kKeyboardGlobal = 0xe2e148;
constexpr uintptr_t kKeyboardWordOffset = 0x160;
constexpr uint32_t kKeyboardSkipMask = 0x8050000;
constexpr uintptr_t kMouseGlobal = 0xe2e16c;
constexpr uintptr_t kMouseButtonsOffset = 0x40;
constexpr uint8_t kMouseSkipBit = 2;

// state mode: phase 13 (push blocks; docs/RE0_NOTES.md "State mode: props")
// uObstacle's per-frame update (vtable slot 41 of 0xcc8140, fastcall, this in ECX, no arguments); its position is at +0x40.
constexpr uintptr_t kObstacleMoveFunction = 0x4e7830;
// The obstacle a pushing character moves (uPlayerBase +0x6a00, getter 0x50e290); the push is think group 0 state 9 (handler
// 0x501180, entered by changeState(0, 9, 0)), which writes the obstacle's +0x40 / +0x44 / +0x48 and its fourth word 0.
constexpr uintptr_t kPlayerPushedObstacleOffset = 0x6a00;
constexpr int32_t kPushThinkGroup = 0;
constexpr int32_t kPushThinkState = 9;
constexpr uintptr_t kObstacleFourthWordOffset = 0x4c;
// The table a room script's obstacle unit word (p2 0..4) indexes: rows of {u8 class, u8 kind, u16 n-th of that kind}. The unit
// registry finds the unit (0x410070, thiscall on [kUnitRoomRegistryGlobal] (scene, class, kind, n-th) ret 0x10 -> unit
// index or kNoUnit); kUnitObjectFunction turns the index into the object.
constexpr uintptr_t kObstacleUnitTable = 0xcd5398;
constexpr uintptr_t kObstacleUnitRowSize = 4;
constexpr uintptr_t kUnitFindFunction = 0x410070;

// Enemies: sEnemy's pool is its seven per-class tables back to back; the pool slot index is the enemy's network id.
constexpr uintptr_t kEnemyGlobal = 0xdcdc78;  // sEnemy*
constexpr uintptr_t kEnemyPoolOffset = 0x4b0;
constexpr uintptr_t kEnemyPoolEntrySize = 16;
constexpr uintptr_t kEnemyPoolObjectOffset = 0xc;  // uEnemy* inside a pool entry
// The "pool" is seven per-class tables of 20 weak handles {vtable, next, prev, uEnemy*} back to back from +0x4b0 (sEnemy
// ctor 0x416560); a class's start (vtable slot 32) find-or-adds the enemy with its table's insert, thiscall (enemy) ret 4,
// returning the index (20 when the table is full). Slot = (table - kEnemyPoolOffset) / kEnemyPoolEntrySize + index, so the
// pool has 7 x 20 slots. (It was 37 until 2026-10-25: uEnemy2b/2c insects took slots 60..81, were never streamed, and each
// machine ran them on its own.)
struct EnemyTableInsert {
    uintptr_t function;
    uintptr_t table;  // sEnemy offset of the table's first handle
};
constexpr std::array<EnemyTableInsert, 7> kEnemyTableInserts = {{
    {0x416c40, 0x4b0},  // the base family (uEnemy10 .. 1a, 50, 51, 5a, 5b, 6a, 6b; start 0x420ab0)
    {0x416a00, 0x5f0},  // uEnemy2a (start 0x4d75c0)
    {0x416b20, 0x730},  // uEnemy31 (start 0x45b0a0)
    {0x4168e0, 0x870},  // uEnemy2b (start 0x438f10)
    {0x416970, 0x9b0},  // uEnemy2c (start 0x443340)
    {0x416bb0, 0xaf0},  // uEnemy34 (start 0x460dc0)
    {0x416a90, 0xc30},  // uEnemy30 (start 0x4536f0)
}};
constexpr uint32_t kEnemyTableEntries = 20;
constexpr int kEnemyPoolSlots = static_cast<int>(kEnemyTableInserts.size() * kEnemyTableEntries);
static_assert(kEnemyTableInserts.back().table + kEnemyTableEntries * kEnemyPoolEntrySize ==
                  kEnemyPoolOffset + kEnemyPoolSlots * kEnemyPoolEntrySize,
              "the class tables are back to back from kEnemyPoolOffset");
constexpr uintptr_t kEnemyHpOffset = 0x1030;  // i32, dead enemies hold -1
constexpr uintptr_t kEnemyActionOffset = 0x67a4;  // 4 x i32 {state, action id, a, b}, written by the class setAction
// The base family: the 15 vtables whose per-frame update (slot 41) is 0x420f70 (uEnemy10..1a, 50, 51, 5a, 5b, 6a, 6b).
// Their AI decides only in the think step, their state-2 handler (slot 67, thiscall, no arguments): a chain of
// transition checks and a per-action think call that end in setAction; it moves and animates nothing. Their setAction
// (slot 63, shared with 17 other vtables) is 0x4cc670, thiscall (state, id, a, b), ret 0x10, and the only writer of
// their record (no direct writes in their code). The other 23 vtables decide inside their action code.
constexpr uintptr_t kEnemyThinkFunction = 0x41db20;
constexpr uintptr_t kEnemyBaseSetActionFunction = 0x4cc670;
constexpr size_t kEnemySetActionSlot = 63;  // vtable slot (+0xfc): the class's setAction, thiscall (state, id, a, b)
// The action executors (state-1 handler, vtable slot 66, thiscall, no arguments) of 18 non-base vtables: each runs the
// class's per-action tables by action id (+0x67a8), and the action code inside decides the next action by direct
// writes, so a record whose state or id changed across one executor call is an action boundary. Their setAction is
// 0x4cc670 or a class wrapper of it that also resets class sub-state (0x44d820, 0x4650f0, 0x480c20, 0x4bbc70; 0x4b17f0
// jumps to 0x4cc670), all four stack arguments. The executor of 15 more vtables is the base family's (think decides
// there); uEnemy35, 44, 45, 47 have none (slot 66 is the empty stub 0x9e2ac0); uEnemy3bRebecca has no setAction.
constexpr std::array<uintptr_t, 14> kEnemyExecutorFunctions = {
    0x4d6530,  // uEnemy2a
    0x437800,  // uEnemy2b
    0x4423a0,  // uEnemy2c
    0x44d080,  // uEnemy2f
    0x451aa0,  // uEnemy30
    0x45a370,  // uEnemy31
    0x45da30,  // uEnemy34
    0x474250,  // uEnemy37, 38
    0x480320,  // uEnemy39
    0x486810,  // uEnemy3a, 3e
    0x490b10,  // uEnemy3c
    0x49c390,  // uEnemy40, 41, 42
    0x4a8940,  // uEnemy43
    0x4bad30,  // uEnemy46
};
// Which player an enemy chases. Each class picks the nearer of sPlayer's controlled and partner characters in its own
// selector (thiscall, no arguments, plain ret, called once per frame from the class update); it writes the target and
// the distance (and for some classes the height), and it fills +0x67cc / +0x67d0 (the distance to the controlled and
// to the partner character, FLT_MAX when that character is not eligible) just before choosing.
constexpr uintptr_t kEnemyDistanceControlledOffset = 0x67cc;  // f32 distance to sPlayer's controlled character
constexpr uintptr_t kEnemyDistancePartnerOffset = 0x67d0;     // f32 distance to sPlayer's partner
constexpr size_t kEnemyUpdateSlot = 41;  // vtable slot (+0xa4): the class's per-frame update
constexpr uintptr_t kEnemyBaseUpdateFunction = 0x420f70;  // the update the 15 base family vtables share
constexpr uintptr_t kEnemyTargetOffset = 0x6b80;          // base family and uEnemy2b: uPlayerBase* the enemy chases
constexpr uintptr_t kEnemyTargetHeightOffset = 0x6b84;    // base family: f32 target y minus own y
constexpr uintptr_t kEnemyTargetFlatDistanceOffset = kEnemyTargetHeightOffset;  // uEnemy2b: flat (x, z) distance to the target
constexpr uintptr_t kEnemyTargetDistanceOffset = 0x6b88;  // base family: f32 distance to the target
// uEnemy2b (kEnemyOwnClassVtable) has its own selector (called from its update 0x4392a0): same target field; +0x6b84 is the
// flat (x, z) distance to the target there, +0x6b88 a range it does not write.
constexpr uintptr_t kEnemyOwnClassVtable = 0xcbf0d0;
constexpr uintptr_t kEnemyOwnClassTargetSelectFunction = 0x439e90;

// How the owner treats a class's target: SyncOnly = sent to the follower, nothing else; the others add hysteresis
// (target_hysteresis_rule) with that tier's tuning.
enum class TargetPolicy : uint8_t { SyncOnly, LargeBoss, Stalker };

// A class's own nearest-player selector and the fields it fills. preferenceMask: while [enemy + preferenceOffset] &
// preferenceMask is set, the class targets a fixed character and is left alone. An unused offset is 0.
struct TargetSelector {
    std::array<uintptr_t, 2> vtables;  // 0 = unused
    uintptr_t matchUpdate;             // 0 = unused; else every vtable whose update (slot 41) is this one
    uintptr_t function;
    uintptr_t targetOffset;            // uPlayerBase*
    uintptr_t distanceOffset;          // filled from +0x67cc / +0x67d0
    uintptr_t heightOffset;            // target y minus own y
    uintptr_t preferenceOffset;
    uint32_t preferenceMask;
    uintptr_t rebeccaFlagOffset;       // a flag word whose rebeccaFlagMask is set while the target is Rebecca-type
    uint32_t rebeccaFlagMask;
    TargetPolicy policy;
};
constexpr uintptr_t kEnemyPreferenceOffset = 0x6848;  // 0x37, 0x38, 0x43: & 3 = a fixed character is targeted
constexpr uint32_t kEnemyPreferenceMask = 3;
constexpr uintptr_t kEnemy46FlagsOffset = 0x6ce8;  // uEnemy46
constexpr uint32_t kEnemy46LockedMask = 2;         // the target is locked on +0x6d78 / +0x6d88
constexpr uint32_t kEnemy46RebeccaMask = 4;        // set while the target is Rebecca-type
constexpr std::array<TargetSelector, 5> kTargetSelectors = {{
    // The base family (zombies and the like): synced, no hysteresis.
    {.vtables = {}, .matchUpdate = kEnemyBaseUpdateFunction, .function = 0x421b20, .targetOffset = kEnemyTargetOffset,
     .distanceOffset = kEnemyTargetDistanceOffset, .heightOffset = kEnemyTargetHeightOffset,
     .policy = TargetPolicy::SyncOnly},
    // uEnemy31.
    {.vtables = {0xcbfe58}, .function = 0x45b9b0, .targetOffset = 0x6a30, .distanceOffset = 0x6a34,
     .policy = TargetPolicy::LargeBoss},
    // uEnemy37, uEnemy38.
    {.vtables = {0xcc08c0, 0xcc5ae8}, .function = 0x475e20, .targetOffset = 0x6a9c, .distanceOffset = 0x6aa0,
     .preferenceOffset = kEnemyPreferenceOffset, .preferenceMask = kEnemyPreferenceMask,
     .policy = TargetPolicy::LargeBoss},
    // uEnemy43.
    {.vtables = {0xcc25d8}, .function = 0x4b15f0, .targetOffset = 0x73fc, .distanceOffset = 0x7400,
     .preferenceOffset = kEnemyPreferenceOffset, .preferenceMask = kEnemyPreferenceMask,
     .policy = TargetPolicy::Stalker},
    // uEnemy46. It writes no distance of its own (the flat ones at +0x67cc / +0x67d0 are what it compares).
    {.vtables = {0xcc3248}, .function = 0x4ba270, .targetOffset = 0x6d98, .preferenceOffset = kEnemy46FlagsOffset,
     .preferenceMask = kEnemy46LockedMask, .rebeccaFlagOffset = kEnemy46FlagsOffset,
     .rebeccaFlagMask = kEnemy46RebeccaMask, .policy = TargetPolicy::LargeBoss},
}};
constexpr uintptr_t kSetHpFunction = 0x529310;  // thiscall, 1 stack arg; enemies and players
// The game's one random generator (xorshift128 0x684d90 on four words at 0xe2ccb0). An enemy's damage function rolls
// it for the critical hit before applying (slot 76 0x4cbdb0 -> randRange 0x6600c0; class 0x438b00 via 0x660080), so
// a hit replayed with the owner's state rolls the owner's result.
constexpr uintptr_t kRandomStateGlobal = 0xe2ccb0;
using RandomState = std::array<uint32_t, 4>;
// sEnemy's create (thiscall (a, record index, b), ret 0xc, returns the enemy): builds an enemy from the room's spawn
// record this + 0x30 + index * 0x5c (index below 0x4e8) and ends in the class's spawn init (vtable slot 36), which reads
// the record (+0x4 kind, +0x10 spawn id, +0x40 first-spawn marker, +0x50 saved variant) and rolls the random state.
// The table is the unit registry [kUnitRoomRegistryGlobal]. Records 0..999 are the spawns of the game's rooms, written
// into the save; 1000 and up are a free list. The create stores its record index in the enemy (setter 0x5295c0, getter
// 0x526510) at kUnitRecordOffset, -1 for every other sGameChara unit, and kUnitObjectFunction finds the unit of a record.
constexpr uintptr_t kEnemyCreateFunction = 0x410bb0;
constexpr uintptr_t kSpawnTableOffset = 0x30;
constexpr uintptr_t kSpawnRecordSize = 0x5c;
constexpr uint32_t kSpawnTableEntries = 0x4e8;
constexpr uint32_t kSpawnRecordCount = 1000;
constexpr uintptr_t kUnitRecordOffset = 0xff0;
constexpr uintptr_t kSpawnRecordKindOffset = 0x4;
constexpr uintptr_t kSpawnRecordIdOffset = 0x10;

// state mode: phase 16 (session; docs/RE0_NOTES.md "State mode: session")
// The game over request: thiscall on the sGameOver singleton (player, cause) ret 8, called by the death states of the think
// (0x5036f0, 0x503840, 0x503a90), the GameOver script op and the up-cut. It returns at once when the room phase is already Dead
// (0xe) or requested to be, otherwise it requests that phase. Cause 0 is a death.
constexpr uintptr_t kGameOverGlobal = 0xdce11c;  // sGameOver*
constexpr uintptr_t kGameOverRequestFunction = 0x5adca0;
constexpr int32_t kGameOverCauseDeath = 0;

// state mode: phase 15 (boss hand-over; docs/RE0_NOTES.md "State mode: boss hand-over")
// The boss classes the hand-over covers: uEnemy31 (Proto-Tyrant), 37 and 38 (Stinger), 46 (Queen Leech), all Observer
// policy. Marcus (43) and the second Queen Leech form (47) stay Legacy until phase 18. Each has the engine's unit move at
// slot 8 (0x526b30, the CharPause path), its own update at slot 41 and setAction at slot 63 (0x4cc670; 0x4bbc70 for 46).
constexpr std::array<uintptr_t, 4> kBossVtables = {0xcbfe58, 0xcc08c0, 0xcc5ae8, 0xcc3248};
// The boss classes of a room whose fight the rescue reload must not restart: kBossVtables plus Marcus (uEnemy43) and the
// second Queen Leech form (uEnemy47), which stay Legacy.
constexpr std::array<uintptr_t, 6> kBossRoomVtables = {kBossVtables[0], kBossVtables[1], kBossVtables[2],
                                                       kBossVtables[3], 0xcc25d8, 0xcc3710};

// state mode: phase 7 (enemy observers, docs/RE0_NOTES.md "State mode: enemy observers")
// Slot 8 of all 38 enemy vtables and both players: fastcall(this), no stack arguments. When sGameInfo +0x50 bit 0 (the
// CharPause bit, script op 160; tested at 0x526c7d) is set it runs only slot 31 (motion), the root-motion cancel and
// slots 18 and 19 (joints, hit shapes) and skips slot 41, the class's update (jump at 0x526d4e).
constexpr uintptr_t kUnitMoveFunction = 0x526b30;
constexpr uint32_t kCharPauseBit = 0x1;
constexpr uintptr_t kEnemyFlags6a0cOffset = 0x6a0c;  // u32, read by the player's aim filter and weapon hit path 0x52f520
constexpr size_t kEnemyDamageSlot = 35;  // vtable slot (+0x8c): damage(attacker, HitPoint*, HitInfo*), thiscall ret 0xC
constexpr std::array<uintptr_t, 38> kEnemyVtables = {
    0xcbdcd8, 0xcc4f28, 0xcc50a0, 0xcc5218, 0xcc5390, 0xcc5508, 0xcc5680, 0xcc57f8, 0xcc5970, 0xcc3fe8,
    0xcbf0d0, 0xcbf5f8, 0xcbf9a0, 0xcbfbc8, 0xcbfe58, 0xcc01a8, 0xcc0458, 0xcc08c0, 0xcc5ae8, 0xcc0cb0,
    0xcc13d8, 0xcc1618, 0xcc18e8, 0xcc5c60, 0xcc1ce8, 0xcc1e60, 0xcc1fd8, 0xcc25d8, 0xcc2b38, 0xcc2f70,
    0xcc3248, 0xcc3710, 0xcc5dd8, 0xcc5f50, 0xcc60c8, 0xcc6240, 0xcc63b8, 0xcc6530};

// Argument of an enemy's damage function, built by the hit resolver (0x5313b0).
struct HitInfo {
    int32_t rangeTier;
    int32_t attackType;
    int32_t a;
    int32_t b;
    void* attacker;
    uint8_t flag;
    uint8_t reserved[3];
};
static_assert(sizeof(HitInfo) == 24);

// Where a hit landed, in world space: the damage function copies it into the enemy (+0x6aa0 in the 0x420f70 family)
// and tests its height against the enemy's to tell a head hit from a body hit.
struct HitPoint {
    float x;
    float y;
    float z;
};
static_assert(sizeof(HitPoint) == 12);

// Think (brain) objects
constexpr uintptr_t kControlledThinkVtable = 0xccbbe8;  // cPlayerThink
constexpr uintptr_t kPartnerThinkVtable = 0xcc99b8;     // cPlayerSubThink
constexpr uintptr_t kThinkPadSlotOffset = 0x50;         // vtable slot 20, which pad to read
constexpr uintptr_t kControlledThinkPadFunction = 0x4f9f10;  // getPad(0)
constexpr uintptr_t kPartnerThinkPadFunction = 0x4f4be0;     // getPad(1)

// Pose puppet (docs/RE0_NOTES.md "Pose puppet", static analysis of build 17178773).
// sGameInfo +0x50 bit 1 is the script pause (PlayerPause 0x56c620): uPlayerBase::move (test at 0x5131d4) then skips
// the think and the locomotion and only animates, cancels the root motion (unless kPlayerKeepRootMotionOffset is
// set), rebuilds the matrices and the collision shapes. The bit is global, so the puppet holds it for one move only.
constexpr uintptr_t kGameInfoModeFlagsOffset = 0x50;  // u32
constexpr uint32_t kPlayerPauseBit = 0x2;
constexpr uint32_t kTriggerPauseBit = 0x8;  // EventTrigPause 0x56cae0: the trigger scan (0x608ad0) does not run
constexpr uint32_t kDemoPauseBit = 0x800;   // set and cleared with the other three by the cutscene ops (Demo_Start)
constexpr uint32_t kControlPauseBit = 0x2000;  // ControlPause 0x574a00 (ControlContinue clears it)
constexpr uint32_t kEventPhaseBit = 0x4000; // EventPhaseIn 0x56cbe0 sets it with kPlayerPauseBit, EventPhaseOut 0x56cd10 clears both
constexpr uint32_t kCutscenePauseBits = kCharPauseBit | kPlayerPauseBit | kTriggerPauseBit | kDemoPauseBit;
// The bits a scripted event (not an examine) holds: a message shown under one is part of that event (live_menus).
// PlayerPause alone is left out: examine and door scripts set it too.
constexpr uint32_t kScriptEventBits = kCharPauseBit | kTriggerPauseBit | kDemoPauseBit | kControlPauseBit | kEventPhaseBit;
// Every motion request of a character ends in cAnimControl::set, a member of the character at kAnimControlOffset:
// thiscall (layer < 8, motionNo, flags, f32 interp, f32 start), ret 0x14, no return value (0x4144d0).
constexpr uintptr_t kAnimControlOffset = 0xff8;
constexpr uintptr_t kAnimControlSetFunction = 0x4144d0;
constexpr uintptr_t kMotionAvailableFunction = 0x73e650;  // thiscall on the character (motionNo) ret 4: nonzero when its bank holds it
constexpr uintptr_t kPlayerResetFunction = 0x516440;      // thiscall, plain ret: the script's Player_Reset (back to neutral)
// Vtable slot 34 of both players: the damage / grab entry, thiscall with five stack dwords (ret 0x14), 0 = refused.
constexpr uintptr_t kPlayerDamageEntryFunction = 0x512570;
// uModel::Motion layers: 8 of 0x140 bytes from +0x4a0.
constexpr uintptr_t kMotionLayerBaseOffset = 0x4a0;
constexpr uintptr_t kMotionLayerStride = 0x140;
constexpr uintptr_t kMotionNumberOffset = 0x4;    // within a layer: u16, bank in bits 8..11, 0xffff = none
constexpr uintptr_t kMotionFrameOffset = 0x4c;    // f32 current frame
constexpr uintptr_t kMotionFrameMaxOffset = 0x54; // f32 motion length - 1
constexpr uintptr_t kThinkGroupOffset = 0x8;      // within the think: i32 group / state / phase
constexpr uintptr_t kThinkStateOffset = 0xc;
constexpr uintptr_t kThinkPhaseOffset = 0x10;
constexpr uintptr_t kPlayerStatusFlagsOffset = 0x68c4;     // u32, read by enemies
constexpr uintptr_t kPlayerKeepRootMotionOffset = 0x6a70;  // u8, playMotion's flags & 0x10; 0 = the position is the caller's

// state mode: phase 3 (POSE_STATE v2 fields; docs/RE0_NOTES.md "State mode: timeline and POSE v2")
constexpr uintptr_t kPlayerX1034Offset = 0x1034;       // u32, uCharaBase; zombie grabs require 0 (meaning open, probe F1)
constexpr uintptr_t kPlayerHitAttributesOffset = 0x16ec;  // u32, low 10 bits are the hit attributes (0x528ee0 sets, 0x528ec0 clears)
constexpr uintptr_t kPlayerAimTierOffset = 0x6954;     // i32, 0..2: the vertical aim band
constexpr uintptr_t kPlayerManualPitchOffset = 0x695c; // f32, radians: the analog up/down pitch while aiming

// state mode: phase 5 (weapon effect replay; docs/RE0_NOTES.md "State mode: weapon effects", read in the decrypted image)
constexpr uintptr_t kWeaponGlobal = 0xdce0a4;                // sWeapon*
constexpr uintptr_t kEffectGlobal = 0xe2da7c;                // sEffect*
constexpr uintptr_t kEffectFlagsGlobal = 0xdc20dc;           // u32: first dword of the {flags, 0x10} pair every listed effect call passes
constexpr uint32_t kEffectFlagsSecond = 0x10;
constexpr uintptr_t kWeaponPlaySeFunction = 0x532bb0;        // thiscall(sWeapon; chara, soundId, f32 volume) ret 0xc
constexpr uintptr_t kWeaponGenericSeFunction = 0x532ce0;     // stdcall(chara, soundId, f32 volume), Wesker's attack
constexpr uintptr_t kWeaponShootFunction = 0x530ec0;         // thiscall(sWeapon; player, mat4*, kind) ret 0xc
constexpr uintptr_t kMuzzleFlashFunction = 0x50cb40;         // thiscall(player; index, f32 scale) ret 8
constexpr uintptr_t kEffectCreateFunction = 0x55a6d0;        // thiscall(sEffect; out*, owner, set, index, attach, joint, pos*, rot*, flags*, extra) ret 0x28
constexpr uintptr_t kStowThrowableFunction = 0x50f8b0;       // thiscall(player), molotov stow
constexpr uintptr_t kLightWickFunction = 0x50d6e0;           // thiscall(player), molotov wick
constexpr uintptr_t kWaterRippleFunction = 0x532880;         // stdcall(chara, sub, vec3*)
constexpr uintptr_t kMuzzleMatrixFunction = 0x50e490;        // thiscall(player; mat4*, muzzle) ret 8
constexpr uintptr_t kPlayerWeaponUnitOffset = 0x6838;        // uWeapon* held in sub-unit slot 3
constexpr uintptr_t kEffectSetOffset = 0x6790;               // effect set pointer of a character or weapon unit (0x522d20)
constexpr uintptr_t kPlayerAutoPitchOffset = 0x68ec;         // f32: auto-aim pitch, read by the projectile launch
constexpr size_t kEffectHandleBytes = 16;                    // room for the handle sEffect::create writes (two dwords)

// sGamePad
constexpr uintptr_t kGetPadFunction = 0x600630;  // thiscall, 1 stack arg, i < 2
constexpr uintptr_t kPadTableOffset = 0xc24;
constexpr uintptr_t kPadEntrySize = 8;
constexpr uintptr_t kAnalogGetterFunction = 0x600550;  // thiscall, no args, returns a pointer to the analog block
constexpr uintptr_t kPadVtable = 0xce55e0;
constexpr uintptr_t kBlockedPadOffset = 0xc34;  // sGamePad member: the pad getPad returns while input is blocked
constexpr size_t kPadObjectSize = 0x4c0;

// Engine functions (32-bit; thiscall members are invoked through callThiscall)
constexpr uintptr_t kPlayerMove = 0x513190;               // uPlayerBase::move, thiscall, once per frame per player
constexpr uintptr_t kAllocThinkFunction = 0x4f81e0;       // cdecl void*(size, align)
constexpr uintptr_t kPlayerThinkCtorFunction = 0x4f8010;  // thiscall void*(self), returns self
constexpr uintptr_t kSetThinkFunction = 0x50f980;         // thiscall void(player, think), destroys the previous think
constexpr uintptr_t kSetControlledFunction = 0x4ecda0;    // sPlayer::setControlled, thiscall, 1 stack arg
constexpr uintptr_t kSetPartnerFunction = 0x4ece00;       // sPlayer::setPartner, thiscall, 1 stack arg
// sUnit::updateAll (vtable slot 6, thiscall, no args, plain ret), once per frame: walks every unit group through
// slot 9 (0x7279e0, thiscall, group index, ret 4), which calls each unit's vtable slot 8 (the move chain).
constexpr uintptr_t kUnitUpdateAllFunction = 0x727b50;
// sUnit stop reasons (docs/RE0_NOTES.md "Live menus"): setStopReason 0x652ce0 (thiscall on [kUnitSystemGlobal]
// (reason < 14, on), ret 8) keeps a reason mask at +0x33d8d0 and gives every group whose bit is in the union of the
// reasons' masks (table 0xdb5ef0) the skip flag slot 9 tests (group +0x28 bit 1). The pause menu (sGamePause) is
// reason 6; the screens' reasons are live_menu_rule::screenStops.
constexpr uintptr_t kUnitSystemGlobal = 0xe2d434;
constexpr uintptr_t kUnitStopMaskOffset = 0x33d8d0;
constexpr uintptr_t kSetStopReasonFunction = 0x652ce0;
constexpr std::array<uint8_t, 7> kSetStopReasonBytes = {0x56, 0x8B, 0xF1, 0x80, 0x7E, 0x1C, 0x00};
constexpr uint32_t kStopReasonPause = 6;

// state mode: phase 4 (puppet body fidelity; docs/RE0_NOTES.md "State mode: puppet body")
// A character holds 7 sub-unit slots of 0x20 bytes from +0x67d8; slot 3 is the held weapon. A slot starts with the
// unit pointer and embeds its cAnimControl 4 bytes in (0x521a80 adds 4 before it calls cAnimControl::set).
constexpr uintptr_t kSubSlotOffset = 0x67d8;
constexpr uintptr_t kSubSlotStride = 0x20;
constexpr uintptr_t kSubControlInSlot = 0x4;
constexpr uintptr_t kPlayerStatusLockOffset = 0x67a0;      // CRITICAL_SECTION (0x18 bytes, up to the think) guarding +0x68c4
constexpr uint32_t kPlayerStatusMask = 0x1ffff;            // the bits 0x512050 keeps when it clears flags
constexpr uintptr_t kPlayerSetHitAttributesFunction = 0x528ee0;    // thiscall (mask) ret 4: |= mask
constexpr uintptr_t kPlayerClearHitAttributesFunction = 0x528ec0;  // thiscall (mask) ret 4: &= ~mask & 0x3ff
constexpr uintptr_t kPlayerHandPoseFunction = 0x50f910;    // fastcall (player): hand pose from motion and weapon type
constexpr uintptr_t kPlayerSpineBendFunction = 0x518f70;   // fastcall (player): spine joints bent by +0x695c, after slot 31
constexpr uintptr_t kPlayerLightProbeFunction = 0x50a880;  // fastcall (player), vtable slot 47: branch B of move only
constexpr uintptr_t kPlayerHandSwapFunction = 0x5178b0;    // fastcall (player): frame-driven hand / holster swaps, move step G
constexpr uintptr_t kPlayerRoomRecordFunction = 0x526490;  // thiscall (player) -> room record (+0xff4)
constexpr uintptr_t kAreaGlobal = 0xdce9dc;                // the object 0x53faa0 runs on
constexpr uintptr_t kAreaIdFunction = 0x53faa0;            // thiscall on kAreaGlobal (record, vec3*, mode) ret 0xc -> area id
constexpr uint32_t kAreaIdMode = 2;                        // what move passes (0x514a41)
constexpr uintptr_t kPlayerSetAreaIdFunction = 0x5295a0;   // thiscall (areaId) ret 4: +0x16e8
constexpr uintptr_t kUnitMotionUpdateFunction = 0x5286c0;  // vtable slot 31 of every uModel unit: fastcall (unit)
constexpr uintptr_t kThinkRumbleFunction = 0x4fac40;       // cPlayerThink slot 15, thiscall (strength) ret 4
constexpr uintptr_t kThinkOwnerOffset = 0x4;               // within the think: the character it belongs to

constexpr uint32_t kPlayerThinkSize = 0xd0;
constexpr uint32_t kPartnerThinkSize = 0x1a0;  // cPlayerSubThink
// Partner code may still treat the partner's think as a cPlayerSubThink and touch its larger layout, so a
// cPlayerThink handed to a partner is allocated at the larger size with the tail zeroed.
constexpr uint32_t kThinkAllocationSize = kPartnerThinkSize > kPlayerThinkSize ? kPartnerThinkSize : kPlayerThinkSize;
constexpr uint32_t kPlayerThinkAlign = 0x10;

// state mode: phase 10 (native rooms; docs/RE0_NOTES.md "State mode: native rooms")
// sRoomControl::changeRoom: thiscall (scene, entry, flags) ret 0xc, the door's commit point. It runs the room change
// 0x61e2c0 before it returns and its only caller is the door thunk 0x552720 (sDoorLoad update phase 4, once per door).
constexpr uintptr_t kChangeRoomFunction = 0x610c60;

// state mode: phase 9 (contact router and grab lease; docs/RE0_NOTES.md "State mode: contact and lease")
// The player damage entry 0x512570 indexes this table by its kind argument: row `kind` is a pair {begin, end} of
// 16-byte entries {i32 think state, i32 motion front, i32 motion back (-1 = none), f32 speed}, entry `level` is at
// begin + level * 16; a null row or a level at or past `end` makes the entry assert and read null. The entry's first word
// is the group-2 think state the victim is put in (changeState(2, state, 0)).
constexpr uintptr_t kReactionTable = 0xd8d6f8;
constexpr uintptr_t kReactionRowSize = 8;
constexpr uintptr_t kReactionEntrySize = 0x10;
// For reference, not hooked: an enemy writes the victim's grab command (+0x69cc) through 0x517340 and the struggle
// reads the pad through 0x4c8ed0, both on the victim's own machine once the enemy runs there.
constexpr uintptr_t kGrabCommandFunction = 0x517340;
constexpr uintptr_t kStruggleFunction = 0x4c8ed0;

// Menu text and the Options screen (docs/RE0_NOTES.md "Menus: text hook and the Co-op entry in Options")
constexpr uintptr_t kGameGuiGlobal = 0xe2daa4;        // sGameGUI*
constexpr uintptr_t kGuiTablesOffset = 0x158;         // rGUIMessage*[kGuiTableSlots], one per message table slot
constexpr int kGuiTableSlots = 19;
constexpr int kGuiOptionTableSlot = 5;                // "option": the Options help lines
constexpr uintptr_t kGetStringFunction = 0x887090;    // thiscall(rGUIMessage table, id) ret 4 -> UTF-8 char*, "" past the count
constexpr uintptr_t kOptionHelpTable = 0xcdf8b8;      // u32 per Options item id: the help line's id in the option table, -1 none
constexpr uintptr_t kOptionTopBuildFunction = 0x5c8970;   // uGUIOptionTop::v5, fastcall(top): fills the item list
constexpr uintptr_t kOptionManagerMoveFunction = 0x5cb5a0;  // uOptionManager::v8, fastcall(manager): the screen router
constexpr uintptr_t kOptionKeyGuideFunction = 0x5cc1b0;   // thiscall(manager, bool) ret 4: the button guide of the screen
constexpr uintptr_t kOptionRouterJumpCheck = 0x5cba3d;    // dec eax; cmp eax, 0x15; ja default: the router's state dispatch
constexpr uintptr_t kOptionRouterDefault = 0x5cc07e;      // default case: unlock and return
// uOptionManager
constexpr uintptr_t kOptionCreatingOffset = 0x48;         // u8: 1 until the pages are built
constexpr uintptr_t kOptionDialogOffset = 0x4d;           // u8: 1 while a confirm dialog is up
constexpr uintptr_t kOptionStateOffset = 0x54;            // u32 router state, 0 closed
constexpr uintptr_t kOptionTopOffset = 0x15c;             // uGUIOptionTop*
// uGUIOptionTop (the field block from +0x264 is read in one copy)
constexpr uintptr_t kOptionTopFieldsOffset = 0x264;       // u8 cancel pressed
constexpr uintptr_t kOptionTopStateOffset = 0x268;        // u32 screen state, 0 closed
constexpr uintptr_t kOptionTopItemsOffset = 0x26c;        // u32[10] item ids in list order
constexpr uintptr_t kOptionTopCountOffset = 0x294;        // u32 item count
constexpr uintptr_t kOptionTopScrollOffset = 0x298;       // u32 first visible item
constexpr uintptr_t kOptionTopCursorOffset = 0x29c;       // u32 cursor row within the visible rows
constexpr size_t kOptionTopFieldsSize = 0x3c;
// Menu input as every screen reads it: the trigger bits of pad, keyboard and mouse merged (cdecl() -> bits)
constexpr uintptr_t kMenuTriggerFunction = 0x5ef420;
constexpr uintptr_t kPadDecideMaskOffset = 0xb6c;         // sGamePad: the trigger bit mask of confirm
constexpr uintptr_t kPadCancelMaskOffset = 0xb70;         // sGamePad: the trigger bit mask of cancel

// MSVC cannot declare a free thiscall pointer; __fastcall with edx unused has the same register and stack layout.
template <class R, class... Args>
R callThiscall(uintptr_t function, void* self, Args... args) {
    return reinterpret_cast<R(__fastcall*)(void*, void*, Args...)>(function)(self, nullptr, args...);
}

inline void* allocThink() {
    void* memory = reinterpret_cast<void*(__cdecl*)(uint32_t, uint32_t)>(kAllocThinkFunction)(kThinkAllocationSize,
                                                                                               kPlayerThinkAlign);
    if (memory) std::memset(memory, 0, kThinkAllocationSize);
    return memory;
}
inline void* constructPlayerThink(void* memory) { return callThiscall<void*>(kPlayerThinkCtorFunction, memory); }
inline void setThink(void* player, void* think) { callThiscall<void>(kSetThinkFunction, player, think); }

// Code decryption probe: the SteamStub-decrypted prologue of cPlayerThink's pad getter
constexpr uintptr_t kDecryptProbeAddress = kControlledThinkPadFunction;
constexpr std::array<uint8_t, 9> kDecryptProbeBytes = {0x8B, 0x0D, 0xC8, 0xD7, 0xE2, 0x00, 0x6A, 0x00, 0xE8};

using PadBytes = std::array<uint8_t, kPadObjectSize>;

// Exception-safe read of a trivially copyable value from game memory.
template <class T>
bool readMemory(uintptr_t address, T& out) {
    if (address == 0) return false;
    __try {
        std::memcpy(&out, reinterpret_cast<const void*>(address), sizeof(T));
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Exception-safe write of a trivially copyable value into game memory.
template <class T>
bool writeMemory(uintptr_t address, const T& value) {
    if (address == 0) return false;
    __try {
        std::memcpy(reinterpret_cast<void*>(address), &value, sizeof(T));
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Writes a pointer-sized value into read-only game memory (vtables).
inline bool writeProtected(uintptr_t address, uint32_t value) {
    DWORD oldProtect = 0;
    if (!VirtualProtect(reinterpret_cast<void*>(address), sizeof(value), PAGE_READWRITE, &oldProtect)) return false;
    *reinterpret_cast<volatile uint32_t*>(address) = value;
    VirtualProtect(reinterpret_cast<void*>(address), sizeof(value), oldProtect, &oldProtect);
    return true;
}

// Pointer stored at address, or 0 when unreadable.
inline uintptr_t readPointer(uintptr_t address) {
    uint32_t value = 0;
    return readMemory(address, value) ? value : 0;
}

// getPad returns a dummy pad (sGamePad + 0xc34, a different class) while input is blocked; only the real
// pad class answers the query slots this adapter records and replays.
inline bool isRealPad(const void* object) {
    return object && readPointer(reinterpret_cast<uintptr_t>(object)) == kPadVtable;
}

// True when the game memory at `address` holds exactly `expected`: the check before hooking or patching a site.
template <size_t N>
bool bytesMatch(uintptr_t address, const std::array<uint8_t, N>& expected) {
    std::array<uint8_t, N> actual{};
    return readMemory(address, actual) && actual == expected;
}

inline bool codeDecrypted() { return bytesMatch(kDecryptProbeAddress, kDecryptProbeBytes); }

// Pointer stored at sPlayer + offset, or 0 when sPlayer or the field is missing.
inline uintptr_t playerField(uintptr_t offset) {
    const uintptr_t player = readPointer(kPlayerGlobal);
    return player ? readPointer(player + offset) : 0;
}

// Address of sGameInfo's mode flags word (kGameInfoModeFlagsOffset), or 0 before sGameInfo exists.
inline uintptr_t modeFlagsAddress() {
    const uintptr_t info = readPointer(kGameInfoGlobal);
    return info ? info + kGameInfoModeFlagsOffset : 0;
}

inline uintptr_t controlled() { return playerField(kPlayerControlledOffset); }
inline uintptr_t partner() { return playerField(kPlayerPartnerOffset); }

// Makes `player` the partner with a fresh cPlayerSubThink (partner AI).
inline void setPartner(uintptr_t player) {
    void* sPlayer = reinterpret_cast<void*>(readPointer(kPlayerGlobal));
    if (sPlayer) callThiscall<void>(kSetPartnerFunction, sPlayer, reinterpret_cast<void*>(player));
}

// Queues a room phase (room_phase.h) on sRoomControl.
inline void requestRoomPhase(int32_t phase) {
    void* control = reinterpret_cast<void*>(readPointer(kRoomControlGlobal));
    if (control) callThiscall<void>(kRequestRoomPhaseFunction, control, phase);
}

// Swaps the camera character: `next` becomes the controlled player and `previous` the partner.
inline void swapControlled(uintptr_t next, uintptr_t previous) {
    void* sPlayer = reinterpret_cast<void*>(readPointer(kPlayerGlobal));
    if (!sPlayer) return;
    callThiscall<void>(kSetControlledFunction, sPlayer, reinterpret_cast<void*>(next));
    setPartner(previous);
}

// The game's random state (kRandomStateGlobal); zeros when unreadable.
inline RandomState readRandomState() {
    RandomState state{};
    readMemory(kRandomStateGlobal, state);
    return state;
}

inline void writeRandomState(const RandomState& state) { writeMemory(kRandomStateGlobal, state); }

inline bool readTransform(uintptr_t player, float (&pos)[3], float (&quat)[4]) {
    return player && readMemory(player + kPlayerPositionOffset, pos) &&
           readMemory(player + kPlayerRotationOffset, quat);
}

inline bool writeTransform(uintptr_t object, const float (&pos)[3], const float (&quat)[4]) {
    return writeMemory(object + kPlayerPositionOffset, pos) && writeMemory(object + kPlayerRotationOffset, quat);
}

}  // namespace game
