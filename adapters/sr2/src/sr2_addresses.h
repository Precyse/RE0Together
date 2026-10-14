#pragma once
#include <cstddef>
#include <cstdint>

// Addresses of SR2_pc.exe (Steam build 1076239 with the Juiced patch, image base 0x400000). Source: docs/SR2_NOTES.md.
namespace sr2 {

// Functions.
constexpr uintptr_t kNetSetMode = 0x7F6F50;           // cdecl(int mode); mode 1 = co-op
constexpr uintptr_t kSessionPoolSetup = 0x7F91C0;     // sizes the connection and packet pools from the limits below
constexpr uintptr_t kRemotePlayerGetter = 0x9D7BD0;   // first human that is not the local player, or null
constexpr uintptr_t kPlayerSlotAllocator = 0x9D7C10;  // refuses a slot index >= kMaxPlayers
constexpr uintptr_t kJoinQueueAdd = 0x810220;         // stdcall(session, request); queues a join request

// Net limits that net_set_mode writes per mode.
constexpr uintptr_t kMaxSessionPlayers = 0xE8C434;
constexpr uintptr_t kMaxPlayers = 0xE8C438;
constexpr uintptr_t kConnectionsPerPlayer = 0xE8C448;

// net_set_mode's co-op case stores ebp (= 2) into the two limits with these instructions.
constexpr uintptr_t kStoreMaxSessionPlayers = 0x7F71EC;
constexpr uintptr_t kStoreMaxPlayers = 0x7F71F2;

constexpr int kNetModeCoop = 1;

// The session object's join request queue count.
constexpr uintptr_t kSessionJoinQueueCountOffset = 0x7EC;

// Co-op ships with 2 players. The N=8 deltacomp instances that replace co-op's 1-peer ones hold up to 8.
constexpr uint32_t kCoopDefaultPlayers = 2;
constexpr uint32_t kMaxReplicatedPlayers = 8;

// net_set_mode's co-op case: the npc limit store (mov [0xE8C43C], 150) and the allocator-set activation. Activation
// reads the player limit, so the adapter's thunk replaces the call; the sets are loaded by name through
// kActivateAllocatorSet(edi = name). Leaving co-op deactivates the same set.
constexpr uintptr_t kCoopNpcLimitStore = 0x7F71F8;
constexpr uintptr_t kActivateCoopSetsCall = 0x7F7220;  // call kActivateAllocatorSet
constexpr uintptr_t kActivateAllocatorSet = 0x7F6CE0;
constexpr uintptr_t kLoadCoopSetNameActivate = 0x7F71D5;    // mov edi, <set name>
constexpr uintptr_t kLoadCoopSetNameDeactivate = 0x7F7083;  // mov edi, <set name>
constexpr uintptr_t kCoopSetNameImmediateOffset = 1;        // the name pointer inside the mov
constexpr uint32_t kSetNameCoopMp = 0xE262B0;               // "coop-mp": the eight N=1 allocators
constexpr uint32_t kSetNameDeltacompSa = 0xE262B8;          // "deltacomp_sa": the eight N=8 allocators
constexpr uint32_t kCoopNpcLimit = 150;

// The co-op player memory pools: bump allocator objects in .data (init 0xBF4A10, alloc 0xC00DB0), created at
// start-up by the arena setup 0x51D800 with a `push size` and a base inside the arena.
constexpr uintptr_t kCoopPlayerCpuPool = 0x2773F0C;
constexpr uintptr_t kCoopPlayerGpuPool = 0x2773F68;
constexpr uintptr_t kCoopPlayerCpuSizePush = 0x51E502;  // push 0x228000
constexpr uintptr_t kCoopPlayerGpuSizePush = 0x51E8A7;  // push 0x1071000
// Holds the coop-mp, multiplayer and deltacomp_global allocator sets, so the deltacomp baselines.
constexpr uintptr_t kCoopAllocatorsPool = 0x2773FC0;
constexpr uintptr_t kCoopAllocatorsSizePush = 0x51E535;  // push 0x5ADBB0
// Pool that allocator sets read at activation: co-op points it at kCoopAllocatorsPool, Strong Arm at its own.
constexpr uintptr_t kCurrentDeltacompPool = 0xFA23A4;

// Pool object layout (bytes from the object).
constexpr uintptr_t kPoolNameOffset = 0xC;
constexpr size_t kPoolNameLength = 32;
constexpr uintptr_t kPoolBaseOffset = 0x38;
constexpr uintptr_t kPoolSizeOffset = 0x3C;      // the size the "Failed aligned alloc" message prints
constexpr uintptr_t kPoolUsedOffset = 0x44;      // bump offset from the base
constexpr uintptr_t kPoolMarkOffset = 0x48;      // restore point of the used offset
constexpr uintptr_t kPoolLimitOffset = 0x4C;     // free bytes = limit - used
constexpr uintptr_t kPoolCapacityOffset = 0x50;  // the size again

// Static allocator object layout (bytes from the object).
constexpr uintptr_t kAllocatorNameOffset = 0x8;           // pointer to the set name
constexpr uintptr_t kAllocatorBlockOffset = 0x14;         // the block taken at activation, 0 while inactive
constexpr uintptr_t kAllocatorCountOffset = 0x18;
constexpr uintptr_t kAllocatorPoolSourceOffset = 0x2C;    // address holding the pool pointer; 0 = the pool at +0x1C
constexpr uintptr_t kPlayerN8Allocator = 0x2A4AE70;       // player_data_bundle, 8-peer instance

// Session object: [kSessionPointer] -> session. Members form a circular list.
constexpr uintptr_t kSessionPointer = 0x2528C14;
constexpr uintptr_t kSessionMemberListOffset = 0x400;
constexpr uintptr_t kSessionLocalMemberOffset = 0x408;
constexpr uintptr_t kMemberNextOffset = 0x22CC;
constexpr uintptr_t kMemberStateOffset = 0x3C;
constexpr uintptr_t kMemberSlotOffset = 0x1B0;  // byte: the session slot the 8-peer deltacomp arrays are indexed by

// Players (milestone 4). A human is a player or npc object; the player humans form the list at kPlayerListHead.
constexpr uintptr_t kCoopFlag = 0x2528B49;  // byte: a co-op session is running
constexpr uintptr_t kPlayerListHead = 0x217043C;
constexpr uintptr_t kLocalPlayer = 0x21703D4;
constexpr uintptr_t kHumanNextOffset = 0x1294;
constexpr uintptr_t kObjectNetRecordOffset = 0xB0;  // pointer to the net record; its byte 0 is the owner's session slot
constexpr uintptr_t kObjectHandleOffset = 0x44;
constexpr uintptr_t kObjectPositionOffset = 0x14;   // three floats
constexpr uintptr_t kHumanJoinFlagsOffset = 0x1161;
constexpr uint8_t kHumanJoiningBit = 0x10;
constexpr uintptr_t kHumanDownedTimerOffset = 0xF8C;  // int, -1 while not down
constexpr uintptr_t kHumanVehicleOffset = 0xD74;      // handle of the vehicle the human is in
constexpr uintptr_t kHumanDrivenVehicleOffset = 0x18D0;
constexpr uintptr_t kHumanMoverOffset = 0x374;
constexpr uintptr_t kHumanActionOffset = 0x1130;
constexpr uint32_t kActionEnterVehicleFirst = 0x1C5;  // actions 0x1C5..0x1CA: getting into a vehicle
constexpr uint32_t kActionEnterVehicleCount = 6;

// Session members: host member and the list a member keeps of the object handles it knows.
constexpr uintptr_t kSessionHostMemberOffset = 0x404;
constexpr uintptr_t kMemberSendEnabledOffset = 0x1B8;
constexpr uintptr_t kMemberKnownHandlesOffset = 0x1DF0;
constexpr uintptr_t kMemberKnownCountOffset = 0x21F0;
constexpr uint32_t kMemberStateReady = 0xB;   // isCoop counts a member from this state on
constexpr uint32_t kMemberStateInGame = 0xD;
constexpr uintptr_t kMaxWalkedPlayers = 12;  // the session is built for 12; every list walk stops here

// Functions the milestone 4 thunks call or replace.
constexpr uintptr_t kPartnerMemberGetter = 0x8CA380;  // cdecl, first member that is not the local one
constexpr uintptr_t kSendToMember = 0x814670;        // ESI = member; cdecl(buffer, length, callback, context)
constexpr uintptr_t kLookSyncA = 0x82B9F0;           // ECX = member, EAX = local human
constexpr uintptr_t kLookSyncB = 0x82BC50;           // ECX = member, EAX = local human
constexpr uintptr_t kResyncHuman = 0x9DFE80;         // fastcall(ECX = human)
constexpr uintptr_t kHoldAssets = 0x6BE1E0;          // cdecl(human, list)
constexpr uintptr_t kTeleportHuman = 0x9DB480;       // thiscall-like: ECX = human, six stack arguments
constexpr uintptr_t kKillLocalHuman = 0x9AF560;      // ESI = human
constexpr uintptr_t kWaitingToRespawn = 0x8CBDB0;    // ESI = human, AL out

// Where the thunks that leave a patched block continue.
constexpr uintptr_t kLeaveTeardown = 0x8CB3A8;
constexpr uintptr_t kLeaveDone = 0x8CB42D;
constexpr uintptr_t kVehicleExitAfterGetter = 0x6A28B9;  // past the getter call that the first patch replaces
constexpr uintptr_t kVehicleExitDone = 0x6A293B;
constexpr uintptr_t kTeamWipeResume = 0x9612DB;

// Player cap (milestone 5). The session info builder stores the advertised and enforced maximum: `and ebx, imm8` then
// `add ebx, 12`, with the imm8 -10 (F6) for co-op (2 players) and 0 otherwise (12). isCoop (0x7F7AD0) refuses to run
// unless the member count is exactly 2; its test is replaced by "some other member is ready".
constexpr uintptr_t kSessionInfoMaxBlock = 0x82FD81;            // 83 E3 F6 83 C3 0C
constexpr uintptr_t kSessionInfoMaxMaskOffset = 2;              // the imm8 inside the block
constexpr uint32_t kSessionInfoBaseMax = 12;                    // what the block adds back (the non-co-op maximum)
constexpr uintptr_t kCoopMemberCountTest = 0x7F7AFD;            // cmp dword ptr [ecx+0x30], 2 ; jne false

// Join path trace (milestone 5b): the host's NAT negotiation gate, the advertised session and the session fields
// the join checks read. Message ids are the first byte of a transport message.
constexpr uintptr_t kNatBusyFlag = 0x212A631;       // byte: a NAT negotiation is running, the next one waits
constexpr uintptr_t kAdvertisedSessionPointer = 0x2528C18;
constexpr uintptr_t kSessionInfoOffset = 0xC;       // pointer to the session info
constexpr uintptr_t kSessionMemberCountOffset = 0x30;
constexpr uintptr_t kSessionBusyStateOffset = 0x1C;  // the states 2, 4, 7..12 refuse a join with reason 5
constexpr uintptr_t kSessionBusySubstateAOffset = 0x20;
constexpr uintptr_t kSessionBusySubstateBOffset = 0x24;
constexpr uintptr_t kSessionReservedOffset = 0x3F4;
constexpr uintptr_t kInfoMaxPlayersOffset = 0x38;
constexpr uintptr_t kInfoJoinTypeOffset = 0x3C;      // 2 = open, 3 = invite only
constexpr uint8_t kMessageJoinReq = 2;
constexpr uint8_t kMessageJoinAcc = 3;
constexpr uint8_t kMessageJoinDeny = 4;
constexpr uint8_t kMessageJoinNewSession = 5;

// Players behind one NAT (milestone 5d). The host drops a join_req as a resend when a member reports the same public
// and private IP; two machines on one home network both report the first address of their host name. The fix compares
// the real source address instead.
constexpr uintptr_t kDuplicateSenderCall = 0x815EAE;     // call kFindDuplicateMember; eax = session, esi = sender block
constexpr uintptr_t kFindDuplicateMember = 0x80FEB0;     // returns the matching member or 0
constexpr uintptr_t kMemberSourceIpOffset = 0x28;        // the real UDP source of a remote member
constexpr uintptr_t kMemberSourcePortOffset = 0x2C;      // word
constexpr uintptr_t kSenderPortOffset = 4;               // word, in the sender block (the IP is at 0)

// Late joiners (milestone 5h). The member state change callback (0x8CB840) queues the host's objects for a member that
// becomes ready only on the host; a guest takes the branch at kGuestMemberBranch.
constexpr uintptr_t kGuestMemberBranch = 0x8CBA34;       // cmp [edi+0x3C], 0xD; jne kMemberCallbackEpilogue
constexpr uintptr_t kMemberCallbackEpilogue = 0x8CBA47;  // pops ebx, esi, edi and returns
constexpr uintptr_t kInGameResetFirst = 0xEB4490;        // set to -1 when a member reaches 0xD
constexpr uintptr_t kInGameResetSecond = 0xEB4494;
constexpr uintptr_t kQueueOwnedObjects = 0x8051F0;       // EDX = member: queue every object we own for creation on it
constexpr uintptr_t kMemberSessionOffset = 0x1AC;        // pointer to the session the member belongs to

// Looks (milestone 5i). The player human setup calls the character instance create at kCharInstanceCreateCall; the components
// message packs the local human's tints with kTintListBuild at kTintSendCall.
constexpr uintptr_t kCharInstanceCreateCall = 0x9D4379;  // call kCharInstanceCreate (its only caller)
constexpr uintptr_t kCharInstanceCreate = 0x9E3BA0;      // cdecl(render object, pool index, is local)
constexpr uintptr_t kTintSendCall = 0x82A17C;            // call kTintListBuild
constexpr uintptr_t kTintListBuild = 0x7D84C0;           // eax = local human, ecx = length out, buffer on the stack

// Ambient population (milestone 5i, section 12). The group create links each group into the active list (circular through
// kPoolLinkOffset) before it fills the group's element slots (+8, one dword each, as many as its template's byte +0x48), and
// takes the groups and elements from two static pools of 2400 whose free lists (circular through kPoolLinkOffset) start at
// kGroupFreeHead and kElementFreeHead.
constexpr uintptr_t kGroupCreate = 0x910370;          // cdecl, 7 stack arguments (the first: the group set), returns the first group or 0
constexpr uintptr_t kGroupSetCountOffset = 0;         // the group set: entry count
constexpr uintptr_t kGroupSetEntriesOffset = 4;       //   and the entry array
constexpr uintptr_t kGroupSetEntryStride = 0x54;
constexpr uintptr_t kEntryElementsOffset = 0;         // an entry: its element count (0 = no group)
constexpr uintptr_t kEntryExtraOffset = 0x28;         //   nonzero = one more element
constexpr uintptr_t kGroupFreeHead = 0x21417B8;
constexpr uintptr_t kElementFreeHead = 0x21417BC;
constexpr uintptr_t kGroupListHead = 0x213F874;
constexpr uintptr_t kPoolLinkOffset = 0x64;
constexpr uintptr_t kGroupTemplateOffset = 4;
constexpr uintptr_t kGroupSlotsOffset = 8;
constexpr uintptr_t kTemplateSlotCountOffset = 0x48;  // byte
constexpr size_t kMaxSpawnGroups = 2400;              // the static group pool

// Member leave (milestone 5i, section 8). The leave function runs on every machine; its loops over the net records are
// patched at the sites below. Net records (list head kRecordListHead, circular through kRecordNextOffset) and the objects
// they stand for (table kObjectTable, indexed by the low 12 bits of the handle).
constexpr uintptr_t kLeaveFunction = 0x8072E0;       // cdecl(leaver slot)
constexpr uintptr_t kLeaveLeaverBranch = 0x807413;   // cmp byte [esi], cl; jne kLeaveOtherOwner
constexpr uintptr_t kLeaveLeaverOwned = 0x80741B;    // the leaver's record: the class decides, destroy or take over
constexpr uintptr_t kLeaveOtherOwner = 0x8074AC;     // another owner's record
constexpr uintptr_t kLeaveNextRecord = 0x8074B8;     // on to the next record
constexpr uintptr_t kLeaveRenameCheck = 0x8074E5;    // the rename loop: the id's slot against the leaver's
constexpr uintptr_t kLeaveRenameBody = 0x8074F2;     // rename the record
constexpr uintptr_t kLeaveRenameSkip = 0x807523;     // on to the next record of the rename loop
constexpr uintptr_t kRecordListHead = 0x212E2AC;
constexpr uintptr_t kRecordOwnerOffset = 0;          // byte: the owner's session slot
constexpr uintptr_t kRecordIdOffset = 2;             // word: the id, its top 4 bits are the creator's slot
constexpr uint32_t kRecordIdSlotShift = 12;
constexpr uintptr_t kRecordHandleOffset = 4;
constexpr uintptr_t kRecordNextOffset = 0x66C;
constexpr uintptr_t kRecordCreatedMaskOffset = 0x67E;  // word: a bit per member slot that has the object
constexpr uintptr_t kObjectTable = 0x2149C64;
constexpr uintptr_t kWideHeaderFlag = 0xE8C30C;      // byte: the transport header is 4 bytes
constexpr uintptr_t kSendMessage = 0x814670;         // cdecl(buffer, length, 0, 0), ESI = member
constexpr uintptr_t kIsHumanHandle = 0x9D7DD0;       // ECX = handle: the object when it is a human, else 0
constexpr uintptr_t kVehicleOfHandle = 0xA009C0;     // ECX = handle: the object when it is a vehicle, else 0
constexpr uintptr_t kExitVehicle = 0x9CAF90;         // cdecl(0, 0): the local player leaves its vehicle
constexpr uintptr_t kUnlinkRecord = 0x804140;        // ESI = record
// Activities (milestone 7). The running mission, its activity instance and the participants.
constexpr uintptr_t kMissionPointer = 0x27B3C60;           // [..] -> the running mission, 0 when none
constexpr uintptr_t kMissionKindOffset = 0x3C;             // 1 = activity
constexpr uint32_t kMissionKindActivity = 1;
constexpr uintptr_t kMissionInstanceOffset = 0x90;         // the activity instance; its vtable +4 returns the type id
constexpr uintptr_t kInstanceTypeIdSlot = 4;               // byte offset in the instance's vtable
constexpr uintptr_t kPendingMissionId = 0x27B3C8C;         // the id a start request waits on
constexpr uintptr_t kMissionOwnerHandle = 0x2527CA0;
constexpr uintptr_t kMissionPartnerHandle = 0x2527CA4;
constexpr uintptr_t kZombieDialog = 0x25276AC;             // handle of the zombie request dialog, 0 when closed
constexpr uintptr_t kSessionHostFlagOffset = 0x3F8;        // byte: this machine hosts the session
constexpr uintptr_t kMessageContextMemberOffset = 4;       // [ctx+4]: the member a message came from
constexpr uintptr_t kHumanRagdollOffset = 0x595;           // byte: the human is in a ragdoll
constexpr uintptr_t kSecurityCelebOwnerOffset = 0x27A8;    // Crowd Control: the object whose vtable +0x34 gives the celeb
constexpr uintptr_t kSecurityCelebSlot = 0x34;             // byte offset in that object's vtable
constexpr uintptr_t kRaceRequestFlag = 0xE8BAF4;           // byte: race_req carries one more header byte
constexpr uintptr_t kRaceRequestSubOffset = 3;             // the race_req sub id in a built message (+1 with the flag)
constexpr uint8_t kRaceSubAccept = 1;
constexpr uint8_t kRaceSubDecline = 2;
constexpr uint8_t kRaceSubInvite = 0;

constexpr uintptr_t kDeliveryVehicleOffset = 0x1C;   // Chop Shop delivery: pointer to the delivered car
constexpr uintptr_t kChopCreditCarArg = 0x538;       // the car argument of the act 0x2B sender, from its return address
constexpr uintptr_t kScriptNameCopy = 0xCDD2F0;      // eax = 0x400 byte buffer, cdecl(name): AL = 1 when the path was built
constexpr size_t kScriptPathBytes = 0x400;
constexpr uintptr_t kSendBroadcast = 0x814740;       // EDI = session; cdecl(buffer, length): every member but the local one
constexpr uintptr_t kFreezeHuman = 0x965F40;         // stdcall(human, flag); sends to the human's owner itself
constexpr uintptr_t kHumanFlagA = 0x968010;          // stdcall(human, flag)
constexpr uintptr_t kHumanFlagB = 0x9682A0;          // stdcall(human, flag)
constexpr uintptr_t kHumanIsBusy = 0x985630;         // cdecl(human), AL out; 0 = free to be moved
constexpr uintptr_t kDeclineHandler = 0x6A8960;      // the host's decline message and cancel

// Milestone 9, the message dispatchers: every handler is called cdecl(payload, context) and the context's [+4] names the
// sender member.
constexpr uintptr_t kMemberLiveOffset = 0x4A;        // byte: the dispatcher forgets a sender whose byte is 0
constexpr uintptr_t kMessageIdOffset = 0;            // byte, in a message
constexpr uintptr_t kMessageLengthOffset = 1;        // word: the payload length, the payload starts 3 bytes in

// Ambient population (milestone 9). The budgets measure from the local camera to each remote human's camera.
constexpr uintptr_t kCameraPosition = 0x25F5B14;       // three floats: the local camera
constexpr uintptr_t kHumanCameraOffset = 0x12A0;       // three floats: a human's camera
constexpr uintptr_t kClusterRadius = 0xE5C660;         // float: how near a partner must be for one spawner to do for both
constexpr uintptr_t kClusterRadiusWide = 0xDD078C;     // float: the same for the callers that ask for the wide test

// Creation queue writers (milestone 9): where the patched appends continue.
constexpr uintptr_t kQueueAppendResumeCreate = 0x805199;  // 0x805000, after the append and `mov cl, [eax+0x1B0]`
constexpr uintptr_t kQueueAppendResumeBatch = 0x80525E;   // 0x8051F0, after the append

// PE header TimeDateStamp of the supported exe (Juiced's patched output keeps the original's).
constexpr uint32_t kExeTimeDateStamp = 0x49904C37;

}  // namespace sr2
