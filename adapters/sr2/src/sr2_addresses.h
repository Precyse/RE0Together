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
constexpr uintptr_t kJoinReqHandler = 0x815BC0;       // cdecl(payload, ctx): the join_req message handler
constexpr uintptr_t kBuildAddressRecord = 0x7FB3D0;   // writes an address record (the new_conn record builder calls it)
constexpr uintptr_t kMapSender = 0x7FA070;            // cdecl(address, kind): the member of a sender, or 0

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
constexpr uintptr_t kSenderIpOffset = 0;                 // dword, in the sender block
constexpr uintptr_t kSenderPortOffset = 4;               // word, in the sender block

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
constexpr uint16_t kNoNetId = 0xFFFF;                // the net id lookup treats it as no object
constexpr uintptr_t kRecordNextOffset = 0x66C;
constexpr uintptr_t kRecordCreatedMaskOffset = 0x67E;  // word: a bit per member slot that has the object
constexpr uintptr_t kObjectTable = 0x2149C64;
constexpr uintptr_t kRecordClassOffset = 8;          // dword: the class (index into kRecordClassTable); the byte goes in messages
constexpr uintptr_t kRecordClassTable = 0x212E25C;   // per class: the object whose vtable +0x14 appends a destroy message's payload
constexpr uintptr_t kClassDestroyPayloadSlot = 0x14; // thiscall(handle, message, int* length)
constexpr uintptr_t kSessionValueOffset = 8;         // dword in the session that every destroy message starts with
constexpr uintptr_t kDestroyWideHeaderFlag = 0xE8C084;  // byte: the rop_destroy header is 4 bytes

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

// The cutscene handshake (milestone 9 MC1): the running cutscene and the bytes cutscn_pkt sets for "a remote did it".
constexpr uintptr_t kCutscene = 0x2527D10;               // pointer to the running cutscene, 0 when none
constexpr uintptr_t kCutsceneSharedSkipOffset = 0xB04;   // byte: one press skips it for everyone
constexpr uintptr_t kCutsceneLocalSkipped = 0x2527D1E;   // byte
constexpr uintptr_t kCutsceneRemoteReady = 0x2527D1D;    // byte
constexpr uintptr_t kCutsceneRemoteSkipped = 0x2527D1F;  // byte
constexpr uintptr_t kCutsceneRemoteDone = 0x2527D20;     // byte
constexpr uintptr_t kCutsceneDoneWait = 0x2527D15;       // byte: the end waits for every remote's done (15 s at most)
constexpr uintptr_t kCutsceneDonePending = 0x2527D21;    // byte: the local done could not be sent at the end
constexpr uintptr_t kCutsceneFadeInAfter = 0xE8DD1F;     // byte: the start argument "fade in after the end"
constexpr uint32_t kCutsceneDoneWaitMs = 15000;

// The screen fade (script fade_in 0xA2E280, fade_out 0xA2E390, the tick 0x4D1790; state 0 none, 1 out, 2 in).
constexpr uintptr_t kFadeState = 0x2526D9C;       // dword
constexpr uintptr_t kFadePercent = 0x2526DA0;     // float
constexpr uintptr_t kFadeTimer = 0xE9D358;        // dword: -1 when no fade runs
constexpr uintptr_t kFadeIgnorePause = 0x2526D95;  // byte: the fade runs while the world is paused

// The script interpreter (Lua 5.0, base library only). Thread errors are silent: the thread runner 0xCE0570 calls
// lua_resume and drops the thread; load errors go through callalert.
constexpr uintptr_t kLuaResume = 0xCDAC30;     // cdecl(nargs), esi = L; the result is nonzero for an error
constexpr uintptr_t kLuaToString = 0xCDCA80;   // ecx = L, eax = stack index; the text or 0
constexpr uintptr_t kLuaCallAlert = 0xCD9060;  // eax = L
constexpr uintptr_t kLuaThreadNameOffset = 0x10;  // char* in the thread record the runner holds
constexpr int kLuaStackTop = -1;

// A world object's chunk id is a word at +0x48 (0xBEEF = none); the chunk query O1 takes it in edi.
constexpr uintptr_t kObjectChunkOffset = 0x48;

// The pause flag (milestone 9 N4, milestone 12): a byte with a bit per pausing slot, read as zero or not.
constexpr uintptr_t kPartnerPaused = 0x252932F;
constexpr uintptr_t kPauseLeaveResume = 0x8CB2A3;  // the member left callback after the clear of the flag and the resume
constexpr uintptr_t kApplyPartnerPause = 0x8CA190;  // no inputs: pauses the world (or the overlay) when the byte is set, resumes when clear
constexpr uintptr_t kPauseRefCount = 0x2527C08;     // dword: pause_world 0x699840 adds one per call, 0x699910 takes one; the world runs at 0
constexpr uintptr_t kWorldPausedFlag = 0x2527FDA;   // byte
constexpr uintptr_t kRemotePauseFlag = 0x2527FDB;   // byte: the pause came from a remote (no message is sent when it ends)

// The local player's name (milestone 12): cdecl(cap) with edi = a wide destination of at most 0x40 characters, AL = 1.
constexpr uintptr_t kLocalNameBuilder = 0x80A070;
constexpr const char* kLocalNameBuilderBytes = "83 EC 44 A1 50 0B E8 00";

// Guest member left (milestone 9 N2a): the cutscene cleanup the guest branch of the callback 0x8CB260 calls.
constexpr uintptr_t kCutsceneCleanup = 0x6C6B90;

// Join admission (milestone 9). The host answers one joiner at a time: the setup_request handler 0x831CA0 keeps the joiner in
// kAdmissionPendingMember and its address block, opens the accept dialog (kAdmissionAcceptDialog) or defers the answer until
// the mission ends (kAdmissionDeferred), and a friends query may run first (kFriendsQueryCallback).
constexpr uintptr_t kAdmissionPendingMember = 0x2528D1C;
constexpr uintptr_t kAdmissionAcceptDialog = 0x2528D14;
constexpr uintptr_t kAdmissionDeferred = 0x2528CF3;       // byte
constexpr uintptr_t kFriendsQueryCallback = 0x2528C40;
constexpr uintptr_t kFriendsQueryHandler = 0x831C40;
constexpr uintptr_t kClearAdmission = 0x8329B0;           // closes the pending joiner and both dialogs
constexpr uintptr_t kSetupResponseWideFlag = 0xE8C174;    // byte: the setup_response header is 4 bytes
constexpr uint8_t kSetupResponseId = 0x19;
constexpr uint8_t kSetupResponseWait = 2;                 // code: wait, the answer comes later

// Creation queue writers (milestone 9): where the patched appends continue.
constexpr uintptr_t kQueueAppendResumeCreate = 0x805199;  // 0x805000, after the append and `mov cl, [eax+0x1B0]`
constexpr uintptr_t kQueueAppendResumeBatch = 0x80525E;   // 0x8051F0, after the append

// The game's one UDP socket (psnet) and the flag that it exists (set at the end of its creation, cleared when it closes),
// and the port every LAN game assumes.
constexpr uintptr_t kGameSocket = 0xE824C0;
constexpr uintptr_t kGameSocketReady = 0x252A608;
constexpr uint32_t kGamePort = 4200;

// Fast fire (milestone 11 B1). Every replicated object plays its input history back against the session's net clock T
// (`[session+8]`). The `clock` message handler sets T from any sender; the tick adds the frame time to it.
constexpr uintptr_t kSessionNetTimeOffset = 8;           // dword: T, in milliseconds
constexpr uintptr_t kMemberHalfRttOffset = 0x223C;       // dword: half the member's round trip time in milliseconds
constexpr uintptr_t kPlayerPool = 0x2CD0590;             // the static pool of player humans
constexpr uintptr_t kPlayerPoolStride = 0x18F0;
constexpr size_t kPlayerPoolCount = kMaxWalkedPlayers;
constexpr uint32_t kHandleIndexMask = 0xFFF;             // the low 12 bits of an object handle index kObjectTable
constexpr uintptr_t kObjectTableStride = 0x10;

// Pool census (milestone 11 B6). The group create 0x910370 has three known callers and the NPC human setup 0x9D0DE0 four (return
// addresses below); the static pools they draw from have these sizes. The NPC humans are a static pool of 150 whose free
// list head is at kNpcHumanFreeHead (circular through kNpcHumanLinkOffset, the layout of the builder at 0x9CF40F).
constexpr size_t kSpawnElementPool = 2400;               // the element pool; the group pool is kMaxSpawnGroups
constexpr uintptr_t kNpcHumanFreeHead = 0x215FFB4;
constexpr uintptr_t kNpcHumanLinkOffset = 0x1160;
constexpr size_t kNpcHumanPool = 150;
constexpr uintptr_t kGroupCreateReturnA = 0x910B58;      // ambient: in 0x910AD0 (the child group spawn), from the group update 0x910DA0
constexpr uintptr_t kGroupCreateReturnB = 0x912E4C;      // crews: in 0x912D80 (the crew builder), from the chunk object loads 0x4AC930 / 0x4ACC30 and the streaming 0x9FC4A0
constexpr uintptr_t kGroupCreateReturnC = 0x9163C5;      // shops: in 0x916390, from the shop interior loader 0x9F2BF0
constexpr uintptr_t kNpcCreateRegister = 0x9D0DE0;       // esi = the human a vtable +0x34 create returned (0 = it failed)
constexpr uintptr_t kNpcCreateReturnA = 0x6D036C;
constexpr uintptr_t kNpcCreateReturnB = 0x9CDF5E;
constexpr uintptr_t kNpcCreateReturnC = 0x9D117D;
constexpr uintptr_t kNpcCreateReturnD = 0x9D1253;

// Population reserve and remote vehicles (milestone 12). Only the group create 0x910370 and the chunk node loader
// kChunkNodeLoad take groups and elements (the game counts them in use at kGroupsInUse / kElementsInUse); the loader's
// return addresses below identify its four callers. A remote vehicle is created through the vehicle class's create slot
// (kVehicleNetCreateSlot, thiscall with 5 stack arguments) from the creation data free list (circular through
// kVehicleCreationLinkOffset) and the static vehicle pool, whose free head is kVehicleFreeHead and in-use count kVehicleCount.
constexpr uintptr_t kGroupsInUse = 0x2529594;
constexpr uintptr_t kElementsInUse = 0x2529590;
constexpr uintptr_t kChunkNodeLoad = 0x9140C0;           // cdecl(int, int), returns al
constexpr uintptr_t kChunkNodeReturnA = 0x914820;
constexpr uintptr_t kChunkNodeReturnB = 0xA25035;
constexpr uintptr_t kChunkNodeReturnC = 0xA7E8B5;
constexpr uintptr_t kChunkNodeReturnD = 0xA815C8;
constexpr uintptr_t kVehicleCount = 0x252A0D4;           // dword: vehicles in use
constexpr size_t kVehiclePool = 32;
constexpr uintptr_t kVehicleFreeHead = 0x22A84D0;
constexpr uintptr_t kVehicleCreationFreeHead = 0x2A4C518;
constexpr uintptr_t kVehicleCreationLinkOffset = 0x284;
constexpr size_t kVehicleCreationPool = 32;
constexpr uintptr_t kVehicleNetCreateSlot = 0xE2BA78;    // the vehicle class vtable +0xC: 0x873F30

constexpr uintptr_t kNpcCreationDataAllocator = 0x2A476F0;  // npc_creation_data, sized from [0xE8C43C]
constexpr uintptr_t kNpcStateDataAllocator = 0x2A47BA0;     // npc_state_data, N=8
constexpr uintptr_t kNpcSimStateAllocator = 0x2A47C90;      // npc_sim_state

// Vehicles (milestone 15; docs/SR2_NOTES.md, "Vehicle pool (milestone 15)" and "Traffic and the vehicle pool"). The category
// node pool counts class 1 (the ambient vehicles) against a limit in the table at 0xE889F4; the traffic list holds every
// machine's traffic (circular, next at +0, the vehicle's handle at +8); a vehicle object is type 5.
constexpr uintptr_t kTrafficListHead = 0x215DA30;
constexpr uintptr_t kTrafficEntryNextOffset = 0;
constexpr uintptr_t kTrafficEntryHandleOffset = 8;
constexpr size_t kTrafficListWalkLimit = 64;
constexpr uintptr_t kObjectTypeOffset = 0x4C;  // dword
constexpr uint32_t kObjectTypeVehicle = 5;
constexpr uintptr_t kRecordNotOwnedOffset = 0x67C;  // byte: bit 0 set = the record is not this machine's to drive
constexpr size_t kObjectTableEntries = 0x1000;
constexpr uintptr_t kCategoryClass1Count = 0x2BA4C64;
constexpr uintptr_t kCategoryClass1Limit = 0xE88A1C;  // dword: 12 shipped
constexpr uint32_t kCategoryClass1ShippedLimit = 12;
constexpr uint32_t kCategoryClass1PerPlayer = 6;
constexpr uintptr_t kCategoryFreeNodes = 0x2BA4C54;  // dword: free vehicle category nodes
constexpr uintptr_t kCategoryFreeHead = 0x2BA4C50;
constexpr uintptr_t kVehicleCreationCount = 0xE8C440;  // dword: the creation data record count (and, doubled, the sim state count)
constexpr uint32_t kVehicleCreationShipped = 0x20;
constexpr uint32_t kVehicleCreationGrown = 0x40;
constexpr uintptr_t kNetRecordsInUse = 0x212BA34;
constexpr size_t kNetRecordPool = 256;
constexpr uintptr_t kTrafficCapBudgetReturn = 0x93D307;  // W1a's caller with base 12, the traffic vehicle cap 0x93D2B0: its return address
constexpr uintptr_t kVehicleDeltacompCount = 0x2A4C3E0;  // the N=8 vehicle deltacomp allocator's count (+0x18)
constexpr uint32_t kVehicleDeltacompShipped = 480;
constexpr uint32_t kVehicleDeltacompGrown = 960;
constexpr uintptr_t kVehicleConstructor = 0xAE1300;         // thiscall(object): the CRT initializer calls it for each pool entry
constexpr uintptr_t kMempoolConstructor = 0xBF48F0;         // thiscall(object): a customisation mempool object
constexpr uintptr_t kVectorConstructor = 0x402450;          // stdcall(first, element size, count, element constructor)
constexpr uintptr_t kT1VectorElementConstructor = 0xAE5720;
constexpr uint32_t kT1VectorElementSize = 0x10;
constexpr uint32_t kT1VectorElementCount = 8;
constexpr uintptr_t kAllocatorFreeHeadOffset = 0x10;
constexpr uintptr_t kAllocatorDynamicOffset = 0x20;  // byte: sized at activation
constexpr uintptr_t kAllocatorActiveOffset = 0x21;   // byte: the block exists
constexpr uintptr_t kAllocatorLockOffset = 0x30;     // the critical section

// Remote player visuals (milestone 14; docs/SR2_NOTES.md, "Remote player visuals (M14)"). A human's visibility flags are a dword
// at +0x80: bit 0x10 hides it, bit 0x04 marks it not shown. A cutscene hides the remotes (0x926860) and its end restores them
// (0x6BF9F0); the skin composite pool is an array of objects the char instances take one each from.
constexpr uintptr_t kHumanVisibilityFlagsOffset = 0x80;
constexpr uint32_t kHumanHiddenFlags = 0x14;
constexpr uintptr_t kHideHuman = 0x926860;             // thiscall(human; hidden, arg, force), the caller pops 12
constexpr uintptr_t kKillHuman = 0x9B04D0;             // cdecl(human, 1)
constexpr uintptr_t kRestoreHuman = 0x6BF9F0;          // cdecl(human): the post cutscene restore
constexpr uintptr_t kHumanHealthOffset = 0x1124;       // float
constexpr uintptr_t kCutsceneStartHideResume = 0x6C0584;
constexpr uintptr_t kCutsceneEndRestoreResume = 0x6C563C;
constexpr uintptr_t kHumanRenderInstanceOffset = 0x5E0;
constexpr uintptr_t kRenderHiddenWordOffset = 0x244;   // word: bit 3 = hidden
constexpr uint16_t kRenderHiddenBit = 8;
constexpr uintptr_t kHumanCharInstanceOffset = 0x13B0;  // pointer to the human's character instance
constexpr uintptr_t kCharInstanceSkinOffset = 8;       // the skin composite object, 0 = none
constexpr uintptr_t kCharInstanceIndexOffset = 0x10;   // the pool index
constexpr uintptr_t kHumanTypeOffset = 0xD4;
constexpr uintptr_t kCharTables = 0x21703D8;           // per pool index {type, record}
constexpr uintptr_t kCharTableStride = 8;
constexpr uintptr_t kCharTableRecordOffset = 4;
constexpr uintptr_t kSkinFreeList = 0x21F297C;
constexpr uintptr_t kSkinUsedList = 0x21F2980;
constexpr uintptr_t kSkinLevels = 0x21F2970;           // set by the pool's build 0x9EEFA0

// Anim set traces (milestone 14): the chunk streamed anim sets, their need table and the root motion decode.
constexpr uintptr_t kStreamingSyncHandler = 0xA83F30;  // cdecl(payload, context); message 0x7E
constexpr uintptr_t kAnimUnload = 0xA841C0;            // thiscall(ecx = group, [esp+4] = slot)
constexpr uintptr_t kAnimDelta = 0xCE4870;             // cdecl(anim entry, t0, t1)
constexpr uintptr_t kAnimNeedTable = 0x2229528;        // 8 rows
constexpr uintptr_t kAnimNeedRowBytes = 0xD0;
constexpr size_t kAnimNeedRows = 8;
constexpr uintptr_t kAnimNeedGroupOffset = 0xCC;       // word
constexpr uintptr_t kAnimNeedLocalOffset = 0xCE;       // byte
constexpr uintptr_t kAnimNeedRemoteOffset = 0xCF;      // byte
constexpr uintptr_t kAnimSlotPools[] = {0x2771AA0, 0x2771AF4, 0x2771B48};  // chunk_anim0 .. chunk_anim2
constexpr uintptr_t kAnimEntryHashOffset = 4;
constexpr uintptr_t kAnimEntrySizeOffset = 0x18;
constexpr uintptr_t kAnimEntryDataOffset = 0x1C;
constexpr uintptr_t kAnimHeaderFlagsOffset = 5;        // byte: bit 0x40 = the header has an offset to decode
constexpr uint8_t kAnimHeaderOffsetFlag = 0x40;
constexpr uintptr_t kAnimHeaderOffsetOffset = 0x2C;    // dword

// Anim and asset streaming per remote (milestone 16 W1, docs/SR2_NOTES.md "Anim and asset streaming per remote (milestone 16 W1)").
constexpr uintptr_t kStreamingSyncHandlerSlot = 0xE8BDCC;  // the message table entry of message 0x7E (kStreamingSyncHandler)
constexpr uintptr_t kStreamingSyncRelFlag = 0xE8BDDC;      // byte: nonzero = the message has a 4 byte header (0xA84032)
constexpr uint8_t kStreamingSubRequest = 0;
constexpr uint8_t kStreamingSubAnswer = 1;
constexpr uint8_t kStreamingSubDrop = 2;                   // "the remote no longer needs the set"
constexpr uintptr_t kStreamingGroupOffset = 1;             // payload fields (0xA83F84..0xA83F99); the sub is the byte at 0
constexpr uintptr_t kStreamingIdOffset = 3;
constexpr uintptr_t kStreamingSlotOffset = 7;
constexpr uintptr_t kStreamingCountOffset = 0xB;
constexpr uintptr_t kStreamingListOffset = 0xF;
constexpr uintptr_t kAnimPendingList = 0xEB8148;           // dword: the pending list, {u16 group, u32 id at +4} of 8 bytes
constexpr uintptr_t kAnimPendingCapacity = 0xEB814C;
constexpr uintptr_t kAnimPendingCount = 0xEB8150;
constexpr uintptr_t kAnimPendingStride = 8;
constexpr uintptr_t kAnimPendingIdOffset = 4;
constexpr uintptr_t kChunkLoadedTable = 0xF7AAE8;          // dword per group, nonzero = loaded
constexpr uint32_t kChunkCount = 0x866;
constexpr uint16_t kNoGroup = 0xFFFF;
constexpr uintptr_t kStreamState = 0x2225B3C;              // dword: the stream state s
constexpr uintptr_t kStreamStateKindOffset = 4;            // [s+4] 1 or 2 = a set is being streamed
constexpr uint32_t kStreamStateKindFirst = 1;
constexpr uint32_t kStreamStateKindLast = 2;
constexpr uintptr_t kStreamStateObjectOffset = 0x10;       // [s+0x10] -> the object of that set
constexpr uintptr_t kStreamObjectNamedGroupOffset = 0x52E; // word: its group
constexpr uintptr_t kStreamEntries = 0x2225B4C;            // dword: the stream entries of 0x28 bytes
constexpr uintptr_t kStreamEntryCount = 0xE87BC0;          // dword
constexpr uintptr_t kStreamEntryStride = 0x28;
constexpr uintptr_t kStreamEntryStateOffset = 4;
constexpr uintptr_t kStreamEntryObjectOffset = 0x10;
constexpr uintptr_t kStreamObjectGroupOffset = 0x772;      // word
constexpr uintptr_t kStreamObjectSlotOffset = 0x778;       // dword: the slot descriptor
constexpr uintptr_t kAnimSlotDescriptors = 0x2771894;
constexpr uintptr_t kAnimSlotDescriptorStride = 0x58;
constexpr uintptr_t kAnimNeedClear = 0xA82600;             // di = group, dl = isLocal
constexpr uintptr_t kAnimNeedInit = 0xA83400;              // the table reset of the level loader
constexpr uintptr_t kAnimNeedTableEnd = 0x2229BA8;
constexpr uintptr_t kAnimSetLoad = 0xA82EA0;               // cdecl(out, group, name, slot, list, count)
constexpr uintptr_t kAnimLoadFromAnswer = 0xA8402A;        // return addresses of kAnimSetLoad
constexpr uintptr_t kAnimLoadFromRequest = 0xA84769;
constexpr uintptr_t kMemberLeftNeedClear = 0x819943;       // call kClearEveryRemoteNeed
constexpr uintptr_t kClearEveryRemoteNeed = 0xA84170;
constexpr uintptr_t kStrmReqSlot = 0xE8BCC4;               // message table entries of the stream request messages
constexpr uintptr_t kStrmRemReqSlot = 0xE8BCDC;
constexpr uintptr_t kCharReqSlot = 0xE8BCF4;
constexpr uintptr_t kCharRemReqSlot = 0xE8BD0C;
constexpr uintptr_t kSetChangeSlot = 0xE8BCAC;             // strm_set_change
constexpr uintptr_t kSetChangeGates = 0x22248B0;           // 8 + 10 time gates, one dword each
constexpr size_t kSetChangeGateCount = 18;
constexpr uintptr_t kStreamReset = 0xA78F10;               // resets both gates
constexpr uintptr_t kMiscTriggerSlot = 0xE8ABF0;           // misc sub 0x16 (Set_Player_Character / Restore_Player_Character)
constexpr uintptr_t kTriggerLocalPreset = 0x2527B64;
constexpr uintptr_t kTriggerPartnerPreset = 0x2527B68;
constexpr uintptr_t kTriggerTick = 0x687B40;
constexpr uintptr_t kTriggerCleanupLoop = 0x688700;
constexpr uintptr_t kTriggerCleanupDone = 0x688763;
constexpr uintptr_t kTriggerReset = 0x687A80;
constexpr uintptr_t kTriggerPartnerLoadCategory = 0x685750; // byte: the category of the game's own partner load, 4 (6 under Juiced)
constexpr uintptr_t kStreamRelease = 0xA79240;             // edx = set; cdecl(category, key, isHost)
constexpr uintptr_t kPresetSetOffset = 8;
constexpr uintptr_t kPresetSetIdOffset = 0x68;

// Outfits, items and weapon slots (milestone 16 W1): the natives' appliers and the name of a player human.
constexpr uintptr_t kItemApply = 0x53FBB0;           // ECX = item name; cdecl(human, option, variant); applies on this machine or sends act 0xC
constexpr uintptr_t kWeaponSlotsApply = 0x796330;    // cdecl(enable, mask); mask bit 1 sends hud 0x17, bit 0 applies here
constexpr uintptr_t kHumanScriptNameOffset = 0x18B0;  // pointer to the text "#PLAYERn#" that scripts name the player by

// One character type per remote (M16 W1; docs/SR2_NOTES.md)
constexpr uintptr_t kCharTypeCategorySetup = 0x9D65D0;  // sizes stream category 3 for the MP characters; returns at once in co-op
constexpr uintptr_t kCoopLoaderMerge = 0x69CB6C;        // the level loader's merge point after the co-op block for styletest_pc_mp2
constexpr uintptr_t kCoopLoaderResume = 0x69CB72;
constexpr uintptr_t kCoopTableFill = 0x9D7FFC;          // the co-op fill of the char table with styletest_pc_mp2
constexpr uintptr_t kCoopTableFillResume = 0x9D8083;
constexpr uintptr_t kStringHash = 0xBDCB30;             // eax = 0, ecx = name, [esp+4] = out; stores the hash, returns the out pointer
constexpr uintptr_t kCharEntries = 0x2526C58;           // pointer to the character type entries
constexpr uintptr_t kCharEntryCount = 0xF5BB68;
constexpr uintptr_t kCharEntryStride = 0x6C;
constexpr uintptr_t kCharEntryHashOffset = 0xC;
constexpr uintptr_t kCharEntryIdOffset = 0x68;          // the stream id; its top byte is the stream category
constexpr uintptr_t kTypeLookup = 0x4A5F60;             // stdcall(name hash, create): pointer to the type, or 0
constexpr uintptr_t kTypeEntryOffset = 8;               // the type's character entry
constexpr uintptr_t kStreamCategories = 0x2F9B2E8;
constexpr uintptr_t kStreamCategoryStride = 0xD4;
constexpr uintptr_t kCategoryCpuSlotOffset = 0x20;
constexpr uintptr_t kCategoryCpuBaseOffset = 0x24;
constexpr uintptr_t kCategoryGpuSlotOffset = 0x28;
constexpr uintptr_t kCategoryGpuBaseOffset = 0x2C;
constexpr uintptr_t kCategoryNodeSizes = 0xA756A0;      // eax = category, edx = id, edi = &cpu size, esi = &gpu size
constexpr uintptr_t kCategoryResetSlots = 0xA75CC0;     // eax = category
constexpr uintptr_t kStreamRequest = 0xA757F0;          // eax = category, stack (id, 5, 0, 1)
constexpr uintptr_t kStreamCategoryPump = 0x946790;     // cdecl(category, 0, 0)
constexpr uintptr_t kStreamLoaded = 0xA79490;           // edi = id; al = loaded
constexpr uintptr_t kRecordRegistry = 0x15B8680;        // pointers to the character records
constexpr size_t kRecordRegistryCount = 0x19;
constexpr uintptr_t kCharRecordIdOffset = 0x60;
constexpr uintptr_t kRecordFlagsOffset = 0x49;          // byte
constexpr uint8_t kRecordHeldFlag = 2;
constexpr uintptr_t kMultiplayerFlag = 0x2528B48;       // byte: a multiplayer (not co-op) session is running
constexpr uintptr_t kLoaderSkipFlag = 0x2527C00;        // byte: the level loader's second entry test of the co-op block

// Kick and session admin (milestone 16 wave 1).
constexpr uintptr_t kDialogCreate = 0x753080;          // eax = default option; cdecl(title, body, options[], count, priority, confirmation, flag), the caller pops 0x1C; returns the dialog record or 0
constexpr uintptr_t kDialogCallbackOffset = 0x930;     // record: the callback cdecl(handle, result, action), called by 0x752D70
constexpr size_t kDialogMaxOptions = 4;                // record +0x514 holds 4 option texts, the count is at +0x914
constexpr size_t kDialogOptionBytes = 0x100;           // one option text slot of the record
constexpr uint32_t kDialogPriorityAction = 1;          // edx of the kick confirm (0x7EF64F)
constexpr int32_t kDialogActionClose = 2;              // Lua DIALOG_ACTION_CLOSE: the dialog was answered
constexpr uintptr_t kDialogNoTextPointer = 0xE8C46C;   // dword: pointer to L"NO"
constexpr uintptr_t kLocalizeText = 0x7F49E0;          // esi = text key, edi = 0; returns the localized wide text
constexpr uintptr_t kKickPlayerTitleKey = 0xE22C54;    // "CONTROL_KICK_PLAYER"
constexpr uintptr_t kSessionRemoveMember = 0x80F9B0;   // eax = session, stdcall(member): removes a member, a remote one on the host first tells every connected member
constexpr uintptr_t kMemberNameOffset = 0xCC;          // wide name, written with a 0x40 character cap
constexpr wchar_t kDialogLiteralMark = 0x01;           // the game's escape for literal text inside a text tag

// HUD and waypoints (milestone 16 wave 2; docs/SR2_NOTES.md, "HUD and waypoints (milestone 16 wave 2)").
constexpr uintptr_t kFollowerBlipsDraw = 0x79F890;         // cdecl(human, scale, color*): the follower blips of one human on the minimap
constexpr uintptr_t kMinimapArrowBody = 0x7A3AA4;          // the minimap's partner arrow block, after its entry test
constexpr uintptr_t kMinimapArrowExit = 0x7A3BD8;          // the join every path of that block reaches
constexpr uintptr_t kMinimapArrowAfterExit = 0x7A3BE2;     // the instruction after the two the exit stub runs
constexpr uintptr_t kHudHalfScale = 0xE5C038;              // qword: the HUD scale the join multiplies by
constexpr uintptr_t kPauseMapArrowBody = 0x77029A;         // the pause map's partner arrow block, after its entry test
constexpr uintptr_t kPauseMapArrowExit = 0x77038F;
constexpr uintptr_t kPauseMapArrowAfterExit = 0x770394;
constexpr uintptr_t kMapViewMode = 0x25F5AE8;              // dword the exit of the pause map block loads
constexpr uintptr_t kHumanBehaviourOffset = 0x149;         // byte
constexpr uint8_t kBackPromptBehaviour = 0x22;             // the behaviour that hides HUD_PRESS_BACK_CANCEL_*
constexpr uintptr_t kHumanFollowerCountOffset = 0xC3C;     // the count of the follower list at +0xC40
constexpr uintptr_t kMissionDebugNative = 0xA398C0;        // Lua native mission_debug: does nothing but return one 0
constexpr uintptr_t kLuaTopOffset = 8;                     // lua_State: the first free stack object
constexpr uintptr_t kLuaBaseOffset = 0xC;                  // lua_State: the first argument of the running native
constexpr uintptr_t kLuaObjectBytes = 0x10;
constexpr uintptr_t kLuaValueOffset = 8;                   // stack object: the value (the string for a string)
constexpr uint32_t kLuaTypeString = 4;                     // the type word at the start of a stack object
constexpr uintptr_t kLuaStringCharsOffset = 0x10;          // string: the characters
// Downed, revive, respawn wait and notoriety (milestone 16 W2, docs/SR2_NOTES.md "Downed, revive and notoriety (M16 W2)").
// The respawn wait is a deadline on the game clock, in milliseconds, that wraps at kClockWrapMs.
constexpr uintptr_t kGameClockMs = 0xE843AC;           // int
constexpr int32_t kRespawnWaitMs = 30000;
constexpr int32_t kClockWrapMs = 1800000000;
constexpr int32_t kClockHalfMs = 900000000;
constexpr uintptr_t kMiscDeathKillResume = 0x8CBC88;   // misc_19 handler: after the kill of the downed local
constexpr uintptr_t kMiscDeathKeepResume = 0x8CBC49;   // misc_19 handler: the local keeps bleeding (pop esi, ret)
constexpr uintptr_t kMissionStartDownedResume = 0x6A02D6;  // mission start: past the block that resets the partner's downed state
constexpr uintptr_t kReleaseDownedHandle = 0x92D4B0;   // cdecl(1, handle, 1, 0), the caller pops 0x10
constexpr uintptr_t kRemoveBlip = 0x79D1B0;            // eax = kBlipReleaseMode, ecx = the blip record
constexpr uintptr_t kHumanBlipOffset = 0xC8C;          // the revive blip marker; 0 = none
constexpr uintptr_t kBlipTable = 0x29E6C28;
constexpr uintptr_t kBlipIdOffset = 0x20;              // record: the marker the blip was created for
constexpr uintptr_t kBlipStride = 0x3C;
constexpr uint32_t kBlipCount = 0x3C;
constexpr uint32_t kBlipIndexMask = 0x1FFF;            // a marker's low bits are the record index
constexpr uint32_t kBlipReleaseMode = 3;
constexpr uint32_t kReleaseDownedFlag = 1;             // first and third argument of kReleaseDownedHandle
constexpr uint32_t kNotorietyTeamCount = 4;
constexpr uintptr_t kNotorietyTable = 0x279AA60;       // host: one record per team
constexpr uintptr_t kNotorietyTeamStride = 0x3C;
constexpr uintptr_t kNotorietyGuestMinOffset = 0x2C;   // record: the guest min locks, one int per lock index
constexpr uintptr_t kNotorietyGuestMaxOffset = 0x34;   // record: the guest max locks
constexpr size_t kNotorietyLockIndexes = 2;
constexpr int32_t kNotorietyNoLock = -1;
constexpr uintptr_t kTeamSeesHuman = 0x55DEC0;         // eax = team, cdecl(human): AL = the team sees it
constexpr uintptr_t kVehicleClassTests[] = {0xAB5140, 0xAB5190, 0xAB51D0};  // eax = vehicle: AL = class 2, 3 or 4 (air and water)
// Milestone 16 wave 2: followers and NPC AI (docs/SR2_NOTES.md, "Homies, followers and NPC AI (milestone 16 wave 2)"). The functions the
// thunks call, the NPC list and the fields of a human they read; a player human has kind 1 at +0x3CC, an NPC human kind 2.
constexpr uintptr_t kRayBlocked = 0x4294C0;            // eax = player, edi = NPC; stack (point), the caller pops; AL = 1 when the ray is blocked, a null player counts as blocked
constexpr uintptr_t kInView = 0x451000;                // esi = player, edi = NPC; AL = the facing or range test
constexpr uintptr_t kSquaredDistance = 0xBDEA30;       // eax, ecx = two vec3 pointers; ST0 = the squared distance rounded to a float
constexpr uintptr_t kSquaredDistanceTeam = 0x404A20;   // the same function, byte for byte
constexpr uintptr_t kEffectiveLeader = 0x98E750;       // edx = human: its second leader when valid and alive, else its leader, else itself
constexpr uintptr_t kNpcTargetOf = 0x43A620;           // eax = NPC: the human it targets, 0 for none
constexpr uintptr_t kNpcHostileTest = 0x43A190;        // eax = NPC; cdecl(target, 0), the caller pops; AL
constexpr uintptr_t kNpcArmedTest = 0x55AE30;          // stdcall(NPC + kHumanInventoryOffset, 1, 0, 0, 0), pops 0x14
constexpr uintptr_t kDismissFollowers = 0x98F460;      // edi = human; cdecl(flag), the caller pops: dismisses its followers, a remote human's owner is told
constexpr uintptr_t kUnlinkAttachment = 0x56BEE0;      // eax = attachment: unlinks it from the list 0x140A1F8
constexpr uintptr_t kSameLeaderTeam = 0x421740;        // eax = human; cdecl(other), the caller pops; AL = both have the same team leader
constexpr uintptr_t kNpcListHead = 0x215FFC0;          // the NPC humans, circular through kNpcHumanLinkOffset
constexpr uintptr_t kGameplayModeType = 0xE987EC;      // dword: the type id the mode enter 0x53B480 stored
constexpr uint32_t kFollowerHandOverReturn = 0x98F7C0; // the return address of the follower tick's hand-over call of 0x98EC10
constexpr uint32_t kDerbyEnterResume = 0x53B744;       // where the derby block of the mode enter goes on
constexpr uint32_t kSepticEnterResume = 0x53B68E;      // where the Septic Avenger block of the mode enter goes on
constexpr int32_t kModeTypeSepticAvenger = 0xC;
constexpr uintptr_t kHumanLeaderOffset = 0x2A4;        // dword: the handle of the human's leader
constexpr uintptr_t kHumanFollowerSlotsOffset = 0xC40; // the handles of the followers, one every kHumanFollowerStride
constexpr uintptr_t kHumanFollowerStride = 0x18;
constexpr uintptr_t kHumanAttachmentOffset = 0x338;    // pointer to the attachment the human carries
constexpr uintptr_t kAttachmentFlagOffset = 0x24;      // byte
constexpr uintptr_t kAttachmentLinkOffset = 0x10;      // dword
constexpr uintptr_t kHumanKindOffset = 0x3CC;          // byte
constexpr uint8_t kHumanKindPlayer = 1;
constexpr uint8_t kHumanKindNpc = 2;
constexpr uintptr_t kHumanTeamByteOffset = 0x3CE;      // byte: compared with the team nibble of a spawn record
constexpr uintptr_t kSpawnRecordTeamOffset = 0xA;      // byte of a spawn record; the low nibble, signed, is the team
constexpr uint32_t kObjectTypeHuman = 1;
constexpr uint32_t kHumanInactiveFlags = 0x0B;         // +0x80 & this is 0 for a human that is in play
constexpr uint32_t kHumanHiddenBit = 0x10;
constexpr float kTeamOddsRangeSquared = 400.0f;        // 20 m: the team odds count the other player and its followers inside this
// Mission flow (milestone 16 W2): the start request and its prompts, the replay prompt, the mission bonus counters.
constexpr uintptr_t kStartRequest = 0x6A8680;          // ecx = &mission id; cdecl(flag byte), the caller pops: asks for (flag 0) or starts (flag 1) the mission
constexpr uintptr_t kStartReply = 0x6A83C0;            // cdecl(reason), the caller pops: mssn_req with the no-mission id; 0 = decline or cancel, 1 = busy, 2 = low respect
constexpr uintptr_t kMissionOwnerHuman = 0x6A80C0;     // eax out: the human the owner handle (0x2527CA0) names, 0 when it is stale; reads globals only
constexpr uintptr_t kMissionPartnerHuman = 0x6A8100;   // eax out: the human the partner handle names, 0 when it is stale
constexpr uintptr_t kBonusAward = 0x6A9640;            // edx = human; cdecl(position, text, flag): the mission bonus line, act 7 to the guests
constexpr uintptr_t kStartPromptDialog = 0x2527C94;    // dword: the accept prompt of this machine, 0 when closed
constexpr uintptr_t kStartWaitDialog = 0x2527C9C;      // dword: the host's wait dialog of its own ask, 0 when closed
constexpr uintptr_t kStartPromptDeferred = 0x2527C87;  // byte: the accept prompt waits to be opened later
constexpr uintptr_t kNoMissionId = 0x33D630C;          // dword: the id that means no mission
constexpr uintptr_t kAcceptResume = 0x6A8B60;          // the host's accept path, past the pending id test
constexpr uintptr_t kAcceptSkip = 0x6A8C98;            // the end of the start request handler
constexpr uintptr_t kHumanNameOffset = 0x12E0;         // wide name of a player human, written with a 0x40 character cap
constexpr size_t kNameChars = 0x40;
constexpr uintptr_t kReplayAsker = 0x25274A4;          // byte: this machine asked for the replay
constexpr uintptr_t kReplayDialog = 0x25274A8;         // dword: the replay wait dialog or prompt, 0 when none
constexpr uintptr_t kReplayCancel = 0x544F10;          // the game's cancel of the replay wait (its only caller is the leave teardown 0x8CB3D9): cdecl void(void)
constexpr uint8_t kReplayAnswerAccept = 3;
constexpr uintptr_t kActHeaderWide = 0xE8BC2C;         // byte: the act header is 4 bytes
constexpr uintptr_t kStartHeaderWide = 0xE8BADC;       // byte: the mssn_req header is 4 bytes
constexpr uintptr_t kReplayHeaderWide = 0xE8BC44;      // byte: the misc header is 4 bytes
constexpr uint8_t kActBonusSub = 7;
// Vehicle seating (milestone 16 wave 2; docs/SR2_NOTES.md "Vehicle seating")
constexpr uintptr_t kVehicleSeatsOffset = 0x8ABC;          // seat array of a vehicle, the occupant's handle at +0 of each seat
constexpr uintptr_t kVehicleSeatStride = 0x24;
constexpr uintptr_t kVehicleSeatsDeclaredOffset = 0x84D8;  // byte: 1 = the vehicle declares its seat count
constexpr uintptr_t kVehicleSeatCountOffset = 0x6CE8;      // dword: the seat count when the byte above is 1
constexpr uintptr_t kVehicleComponentOffset = 0x84EC;      // pointer to the component whose vtable +0x20 the mission exit calls (stdcall(0))
constexpr uintptr_t kComponentStopSlot = 0x20;             // vtable offset of that call
constexpr uintptr_t kHumanInVehicle = 0x9B8C50;            // ESI = human, EDI = vehicle: AL = 1 when settled in it
constexpr uintptr_t kReleaseHeld = 0x69DDA0;               // EDX = human: releases the human it holds (+0xEA8)
constexpr uintptr_t kMissionExitPartnerDone = 0x6A4E85;    // the mission exit after the default partner's stop (SE3 resumes here)
// Milestone 16 wave 2: net handlers, chat, pause (M16_W2_NET.md).
constexpr uintptr_t kHumanCashOffset = 0x1114;          // int: the cash of a human
constexpr uintptr_t kRaceHoldLocal = 0x252981D;         // byte: the local player holds the race resource set
constexpr uintptr_t kRaceHoldRemote = 0x252981E;        // byte: a remote holds it (a bit per session slot with 3 or more players)
constexpr uintptr_t kRaceResourceSet = 0x140C818;       // the name of the race resource set (.mms / .ods), esi of the release
constexpr uintptr_t kRaceResourceRelease = 0xA25760;    // ESI = the set name: unloads it
constexpr uintptr_t kRaceHoldLoad = 0xA25D33;           // scrpt_02 (RH1): the game loads the set
constexpr uintptr_t kRaceHoldReturn = 0xA25D4B;         // scrpt_02 (RH1): the handler returns
constexpr uintptr_t kRaceHoldUnload = 0xA25D7A;         // scrpt_03 (RH2): the game unloads the set
constexpr uintptr_t kRaceHoldSkip = 0xA25D91;           // scrpt_03 (RH2): the handler returns
constexpr uintptr_t kConfessLocal = 0x2528778;          // pointer: the confessional object of the local player
constexpr uintptr_t kConfessLock = 0x252877C;           // pointer: the confessional object a remote is in, 0 when none
constexpr uintptr_t kConfessDistanceLimit = 0xE5C688;   // double: the squared distance a partner may be from the confessional
constexpr uintptr_t kPriceToCents = 0x4B2680;           // ESI = out; stdcall(float price): the price in clamped integer cents
constexpr uint8_t kConfessNoticeFar = 2;                // misc_00 code: a partner is too far away
constexpr uint8_t kConfessNoticePoor = 6;               // misc_00 code: a partner cannot pay
constexpr uintptr_t kConfessShowFar = 0x7A94F7;         // misc_00 (CF8): the game shows "you are too far" (code 3)
constexpr uintptr_t kConfessNotCodeFar = 0x7A950F;      // misc_00 (CF8): the code is not 2
constexpr uintptr_t kConfessShowPoor = 0x7A956D;        // misc_00 (CF9): the game shows "you can't afford" (code 5)
constexpr uintptr_t kConfessNotCodePoor = 0x7A9514;     // misc_00 (CF9): the code is not 6
constexpr uintptr_t kConfessReturn = 0x7A95D7;          // misc_00 (CF8, CF9): the handler returns
// Diversions and activities (milestone 16 wave 2, diversion_sites.h). Streaking: the flashing diversion keeps one byte "the remote
// streaks" and, per human (index 0 the local player, 1 any other), a coat morph object, a loaded flag, its size and its buffer.
constexpr uintptr_t kStreakActiveByte = 0x2527895;
constexpr uintptr_t kStreakCoatMorph = 0x140B26C;     // dword[2]
constexpr uintptr_t kStreakCoatLoaded = 0x140B274;    // byte[2]
constexpr uintptr_t kStreakCoatSize = 0x140B278;      // dword[2]
constexpr uintptr_t kStreakCoatBuffer = 0x140B280;    // dword[2]
constexpr size_t kStreakRemoteEntry = 1;
constexpr uintptr_t kStreakCoatLoad = 0x610780;       // cdecl(human), AL out: loads the coat morph of that human
constexpr uintptr_t kCharacterIdOffset = 0x10;        // dword: the id a built character record and its instance share

// Tow truck and fraud: tests the game keeps in functions of their own.
constexpr uintptr_t kVehicleHolds = 0x9B8CB0;         // ECX = vehicle, EAX = a human handle; AL = that human is in it
constexpr uintptr_t kFraudInZone = 0x64B010;          // ECX = point, EDX = zone; AL
constexpr uintptr_t kFraudInArea = 0x649FB0;          // EDX = point; AL
constexpr uintptr_t kFraudZoneOfHuman = 0x64F210;     // stdcall(human): the zone the human stands in, 0 for none
constexpr uintptr_t kFraudPartnerZone = 0x252797C;    // dword: the zone of the default partner
constexpr uintptr_t kFraudRecordOffset = 0x10;
constexpr uintptr_t kFraudLocalHitOffset = 0x24;      // record: the last object the local player hit
constexpr uintptr_t kFraudPartnerHitOffset = 0x28;    // record: the last object the partner hit

// Snatch: the level holds the carried count of the partner and the two drop-off markers of the level table.
constexpr uintptr_t kSnatchCarryOffset = 0x45C;
constexpr uintptr_t kSnatchMarkerOffset = 0x1C;       // level: dword[2], the drop-off marker of the local side and of the remote side
constexpr uintptr_t kSnatchMarkerFlagsOffset = 0x110; // marker: bit 8 = not accepting deliveries
constexpr uint8_t kSnatchMarkerClosedBit = 8;
constexpr uintptr_t kSnatchDeliver = 0x657880;        // stdcall(level, player)
constexpr uintptr_t kSnatchRadiusInVehicle = 0xDD1C90;  // float
constexpr uintptr_t kSnatchRadiusOnFoot = 0xE8735C;     // float
constexpr uintptr_t kSnatchPartnerTargetOffset = 0x8C;  // instance: the object the partner is tested against to keep the ho's car (the local player's is at +0x88)
constexpr uintptr_t kSnatchHoHomeOffset = 0x90;       // instance: the ho's home object
constexpr uintptr_t kSnatchGroupPointOffset = 0x1C;   // a ho group or home object: the vec3
constexpr uintptr_t kLevelInstanceOffset = 4;         // an activity level: pointer to its instance
constexpr uintptr_t kSnatchWaypointGroupOffset = 0x390;   // level: target group of the remote side's waypoint
constexpr uintptr_t kSnatchWaypointLastOffset = 0x398;    // level: its last target
constexpr uintptr_t kSnatchSetupBoth = 0x9DE310;      // ECX = human, cdecl(3)
constexpr uintptr_t kSnatchSetupLook = 0x9E0350;      // stdcall(human, 1)
constexpr uintptr_t kSnatchSetupWeapons = 0x9DED00;   // cdecl(human, 0)
constexpr uint32_t kSnatchSetupBothArg = 3;
constexpr uintptr_t kVehiclePrepare = 0xAAAB60;       // ECX = -1, EDX = vehicle, cdecl(1.0f, 1, 1)
constexpr uint32_t kFloatOne = 0x3F800000;
constexpr uintptr_t kResumeSnatchSetup = 0x656C43;
constexpr uintptr_t kResumeSnatchVehicles = 0x65DA35;
constexpr uintptr_t kResumeSnatchDelivery = 0x65AF50;

// Trafficking and Mayhem.
constexpr uintptr_t kTraffickingArm = 0x6671B0;       // EAX = level, EDI = human
constexpr uintptr_t kTraffickingDisarm = 0x6671F0;    // EAX = level, stdcall(human)
constexpr uintptr_t kMayhemArm = 0x66ABB0;            // EAX = level, EDI = human
constexpr uintptr_t kMayhemDisarm = 0x66B4C0;         // EAX = level, stdcall(human)
constexpr uintptr_t kMayhemDestroyerOffset = 0x30;    // a destruction record: the human that destroyed it
constexpr uintptr_t kMayhemCredit = 0x668240;         // ESI = human handle, cdecl(0, amount, human, 0)
constexpr uintptr_t kMayhemComplication = 0x9C8C20;   // cdecl(human, flag)
constexpr uintptr_t kVehicleObjectOf = 0xAE28F0;      // ECX = handle: the object, 0 when the handle is stale or its kind (+0x4C) is 5; no stack arguments
constexpr uintptr_t kTraffickingDealerSource = 0x58;  // instance: object that knows the dealer; vtable +0x2C tests, +0x34 returns it
constexpr uintptr_t kDealerSourceHasSlot = 0x2C;
constexpr uintptr_t kDealerSourceGetSlot = 0x34;
constexpr uintptr_t kDealerLeaderOffset = 0x2A4;      // dealer: handle of the human it follows
constexpr uintptr_t kResumeTraffickingVehicles = 0x666D80;
constexpr uintptr_t kResumeMayhemCredit = 0x66864E;
constexpr uintptr_t kSeatPlace = 0xAEFC40;            // EAX = a seat request
constexpr size_t kSeatRequestBytes = 0x1B;
constexpr uint32_t kFirstExtraSeat = 3;

// District: a human holds the district record it stands in; the Mayhem level and the district objective compare it.
constexpr uintptr_t kHumanDistrictOffset = 0xF00;
constexpr uintptr_t kDistrictOfPoint = 0x4B19F0;      // cdecl(point, hint record) -> district index or -1
constexpr uintptr_t kDistrictRecords = 0x2526C94;     // pointer to the district records
constexpr uintptr_t kDistrictRecordStride = 0xA8;
constexpr uintptr_t kDistrictKeyOffset = 0x54;        // record: the key the level compares
constexpr uintptr_t kMayhemTargetIndexOffset = 0x544; // level: index of the target district
constexpr uintptr_t kMayhemTargetStride = 100;
constexpr uintptr_t kMayhemTargetKeyOffset = 0x50;    // in [level+4] + index * stride
constexpr uintptr_t kMayhemAllInOffset = 0x548;       // level byte: every player is in the district
constexpr uintptr_t kMayhemTimerOnOffset = 0x549;     // level byte
constexpr uintptr_t kMayhemPlayerIn = 0x66BCA0;       // ECX = human, stdcall(level)
constexpr uintptr_t kMayhemPlayerOut = 0x66BDA0;      // stdcall(level, human, 1)
constexpr uintptr_t kResumeMayhemDistrict = 0x66C050;
constexpr uintptr_t kDistrictObjectiveTarget = 0xE9E3F4;  // dword: the district record the objective at 0x5664A5 waits for

// The co-op race diversion: a byte that is set while it runs.
constexpr uintptr_t kCoopRaceFlag = 0x2527B4F;

// PE header TimeDateStamp of the supported exe (Juiced's patched output keeps the original's).
constexpr uint32_t kExeTimeDateStamp = 0x49904C37;

// Store, garage and activity level menus (milestone 16 wave 2).
constexpr uintptr_t kPointInBox = 0xBE9890;             // ECX = point, EAX = box centre, ESI = orientation; cdecl(two box arguments), AL out
constexpr uintptr_t kStoreEnterHideResume = 0x740243;   // after the partner hide of the store entry (0x74021C)
constexpr uintptr_t kStoreExitShowResume = 0x7407E1;    // after the partner unhide of the store exit (0x7407BA)
constexpr uintptr_t kGarageHideResume = 0xA6D03C;       // after the partner re-hide of the host's garage (0xA6D015)
constexpr uintptr_t kLevelChoiceSendReturn = 0x75B676;   // return address of the guest's choice broadcast (0x75B671)
constexpr uintptr_t kLevelCancelSendReturn = 0x75B749;   // return address of the guest's cancel broadcast (0x75B744)

// Remote poses, world and HUD walks, garage door, client name (milestone 17 wave 3).
constexpr uintptr_t kAmbulanceRemotePose = 0xE97C8C;    // dword: the CPR anim of "the remote", kNoAnimState for none
constexpr uintptr_t kAmbulancePoseActive = 0x5F7DF0;    // AL out: a CPR pose is set and the partner has no scripted action
constexpr uintptr_t kHumanCurrentAction = 0x96F3F0;     // ESI = human, EAX out: the scripted action in progress, kNoAnimState for none
constexpr uint32_t kNoAnimState = 0xFFFFFFFF;
constexpr uintptr_t kSpawnSeenByHuman = 0x936080;       // ESI = point, EDI = human, cdecl(flag), AL out: seen by or near that human
constexpr uintptr_t kCurrentGarage = 0x2799304;         // pointer to the crib garage the door logic works on, 0 for none
constexpr uintptr_t kGarageBoxOffsets[] = {0x10, 0x08, 0x0C};  // the three boxes a garage holds (pointers at garage + offset)
constexpr uintptr_t kPointInGarageBox = 0x552F80;       // EAX = box, cdecl(point), AL out; false for a null box
constexpr uintptr_t kFollowerBarsPartnerLocal = 0x18;   // 0x797CD0's local: the partner of the follower bars
constexpr uintptr_t kFollowerBarsFirstLocal = 0x24;     // 0x797CD0's locals: the three bar humans, one dword each
constexpr size_t kFollowerBarCount = 3;
constexpr uintptr_t kFollowerBarsResume = 0x797DF4;     // after the walk that fills the bars
constexpr uintptr_t kActHandlerTable = 0xE8AA28;        // act message handlers, one pointer per sub
constexpr uint32_t kActAmbulancePoseSub = 0x4B;         // the CPR pose of the Ambulance diversion
constexpr uintptr_t kMiscHandlerTable = 0xE8AB98;       // misc message handlers, one pointer per sub
constexpr uint32_t kMiscCompletionSub = 7;              // a client's activity completion masks
// Zombie Uprising for 3 to 4 players (milestone 17 wave 3; docs/SR2_NOTES.md, "Zombie Uprising for 3 to 4 players (milestone 17 wave 3)").
constexpr uintptr_t kRecruitSetMode = kSnatchSetupBoth;       // recruit_set_mode's worker: ECX = human, cdecl(mode), 0 none, 1 friendly gang
constexpr uintptr_t kCharacterDontRegenerate = 0x9694A0;      // EDI = human, stdcall(flag), pops its argument
constexpr uintptr_t kEvacuateFromVehicles = 0xB076C0;         // EDI = human, cdecl(flag); the protect start calls it with 1
constexpr uintptr_t kZombieStartMarkerOffset = 0x418;         // zombie level: the start marker record
constexpr uintptr_t kMarkerPositionOffset = 0x1C;             // marker: the vec3 position
constexpr uintptr_t kMarkerOrientationOffset = 0x28;          // marker: the orientation
constexpr uintptr_t kZombiePartnerStartOffset = 0xE5BF40;     // double: the x offset of the partner's start spot from the local player's
constexpr uintptr_t kHumanMaxHealthOffset = 0x1120;           // int32; the health at +0x1124 divided by it is the fraction
// Milestone 17 wave 3: FUZZ (fuzz_sites.h).
constexpr uintptr_t kFuzzSetMode = 0x968930;              // stdcall(human, const uint8_t* mode): human +0x111C, a remote's owner is sent message 0x6E
constexpr uintptr_t kGiveWeapon = 0xB71CF0;               // ECX = human; cdecl, 5 arguments (the weapon info first), the caller pops
constexpr uintptr_t kRemoveWeapon = 0xB723D0;             // ECX = human, EDX = weapon info; cdecl(flag), the caller pops
constexpr uintptr_t kGivePickupWeapon = 0x53D1F0;         // ECX and AL as the caller sets them; cdecl(human, const weapon info*, flag), the caller pops
constexpr uintptr_t kHumanFighting = 0x960C30;            // ESI = human; AL = the human is fighting
constexpr uintptr_t kFuzzCrimeDistrictKeyOffset = 0x1C0;  // crime: the key of the district the crime area is in
constexpr uintptr_t kFuzzRioterHeightWindow = 0xDD522C;   // float: how far above or below a rioter a player counts as near
constexpr uint32_t kFuzzRioterPartnerSlot = 0x14;         // [esp + 0x14] at the rioter block: the human the partner getter returned
constexpr uint32_t kFuzzRioterRadiusSlot = 0x20;          // [esp + 0x20] at the rioter block: the squared radius, a double
constexpr uintptr_t kObjectPositionYOffset = 0x18;        // the height inside an object's position
constexpr uintptr_t kFuzzRioterJoins = 0x5CE6FC;          // where a rioter that is near a player goes on
constexpr uintptr_t kFuzzRioterSkips = 0x5CE7BF;          // where the rioter loop goes on with the next human
constexpr uint32_t kFuzzSpawnPointSlot = 0x8;             // [esp + 8] at the spawn point's partner getter call: the point's vec3
// Milestone 17 wave 3: Crowd Control (M17_W3_CROWD.md). The calls the level start and end make for the local player and "the partner",
// the fields they read and the addresses the jump outs resume at.
constexpr uintptr_t kSyncIgnoreEquipOnGrab = 0x9E0710;  // stdcall(human, flag): sync flag ignore_equip_on_grab (+0x18E8 bit 3), tells the owner
constexpr uintptr_t kForceBeerMuscles = 0x9676E0;       // stdcall(human, flag): force flag beer_muscles (+0x110C bit 25)
constexpr uintptr_t kInventoryApply = 0x55A930;         // stdcall(inventory, id, 1, 0): the weapon equip and the torch effect are cases of it
constexpr uintptr_t kReleaseHeldAtEnd = 0x9B2240;       // ECX = human; cdecl(1): lets go of what the human holds
constexpr uintptr_t kHumanHeldOffset = 0xEA8;           // handle of the human (or celebrity) this human holds, 0 for none
constexpr uintptr_t kHumanInventoryOffset = 0x10C4;     // the inventory of a human
constexpr uintptr_t kCrowdWeaponOffset = 0x74;          // level: the bean bag gun
constexpr uintptr_t kCrowdEventIndexOffset = 0x7C;      // level: the running event
constexpr uintptr_t kCrowdEventStride = 0x16C8;         // [level + 4]: one record per event
constexpr uintptr_t kCrowdNavOffset = 0x60;             // event record: the navs, one dword each (local start, partner start, end)
constexpr uintptr_t kNavPositionOffset = 0x1C;          // nav: the vec3
constexpr uintptr_t kProtectLocalSlot = 0x10;           // protect start: stack slot of the local player's human
constexpr uintptr_t kProtectCelebSlot = 0x18;           // protect start: stack slot of the celebrity
constexpr uintptr_t kResumeProtectStart = 0x5A1B55;
constexpr uintptr_t kResumeCrowdWeapon = 0x5AD8E0;
constexpr uintptr_t kResumeCrowdStartFlags = 0x5AD9C4;
constexpr uintptr_t kResumeCrowdRelease = 0x5ADF19;
constexpr uintptr_t kResumeCrowdEndFlags = 0x5AE224;
// Trail Blazing (milestone 17 wave 3): the torch activity for every remote (docs/SR2_NOTES.md, "Trail Blazing (milestone 17 wave 3)").
constexpr uint32_t kTrailStartEffectId = 0;
constexpr uintptr_t kTrailStartCheck = 0x9C3A90;            // EDX = human: AL
constexpr uintptr_t kTrailStartStep = 0x9C3B00;             // cdecl(human)
constexpr uintptr_t kPutOutHuman = 0x98CEB0;                // EDI = handle; cdecl(flag): puts the burning human out
constexpr uintptr_t kTrailNodeInRange = 0x557F60;           // ECX = fire node; stdcall(x, y, z, radius): AL
constexpr uintptr_t kTrailLightNode = 0x558000;             // stdcall(node, igniter handle)
constexpr uintptr_t kTrailLightObjectNear = 0x58D9B0;       // ESI = flammable object entry, EDI = human
constexpr uintptr_t kExitRequestInit = 0x433190;            // EAX = a 0x24 byte request
constexpr uintptr_t kRequestVehicleExit = 0xB072F0;         // cdecl(human, request)
constexpr uintptr_t kTrailNodeRadius = 0xE38AE0;            // float: how close a rider lights a fire node
constexpr uintptr_t kTrailStartSpacing = 0xE5C130;          // double: the gap between two players at the stage start
constexpr uintptr_t kTrailCheckpointReach = 0xE5C330;       // double: a player this close (squared) takes the checkpoint
constexpr uintptr_t kTrailLevelStateOffset = 0x28;          // level: 2 while the course runs
constexpr uint32_t kTrailLevelRunning = 2;
constexpr uintptr_t kTrailNodesOffset = 0x34;               // level: the list of fire nodes
constexpr uintptr_t kTrailNodeLitOffset = 0xD0;             // fire node byte: lit
constexpr uintptr_t kTrailObjectLitOffset = 4;              // flammable object entry byte: lit
constexpr uintptr_t kHumanSwimmingOffset = 0xE88;           // dword: non-zero while swimming
constexpr uintptr_t kHumanSubmergedFlagsOffset = 0x84;      // byte: bit 0 = submerged
constexpr uintptr_t kHumanSubmergedDepthOffset = 0x94;      // float
constexpr uintptr_t kTrailStartRecordOffset = 4;            // the start argument: the spawn record
constexpr uintptr_t kTrailStartRightOffset = 0x28;          // spawn record: the row to the right (also the orientation the warp takes)
constexpr uintptr_t kTrailStartForwardOffset = 0x40;        // spawn record: the forward row
constexpr uintptr_t kTrailCheckpointPositionOffset = 0x1C;  // checkpoint record: the position
constexpr uintptr_t kExitRequestFlagsOffset = 0x20;         // exit request byte: bit 0 asks to leave
constexpr uintptr_t kTrailFireLocalSlot = 0x10;             // stack at TB6: the local player
constexpr uintptr_t kTrailFailByteSlot = 0x13;              // stack at TB8: the byte that fails the level
constexpr uintptr_t kTrailStartLocalSlot = 0x28;            // stack at TB9: the local player's start position
constexpr uintptr_t kTrailStartPartnerSlot = 0x34;          // stack at TB9: the default partner's start position
constexpr uintptr_t kTrailExitRequestSlot = 0x34;           // stack at TB10: the exit request
constexpr uintptr_t kTrailCheckpointDistanceSlot = 8;       // stack at TB11: the float distance
constexpr uintptr_t kTrailCheckpointPartnerSlot = 0xC;      // stack at TB11: the getter's answer
constexpr uintptr_t kTrailCheckpointTargetSlot = 0x14;      // stack at TB11: the pointer to the checkpoint position
constexpr uintptr_t kResumeTrailLevelStart = 0x58CDF5;
constexpr uintptr_t kResumeTrailLevelStep = 0x58CE4C;
constexpr uintptr_t kResumeTrailFireNodes = 0x58DADA;
constexpr uintptr_t kResumeTrailWaterFail = 0x58DDF4;
constexpr uintptr_t kResumeTrailStartPlace = 0x58EEB1;
constexpr uintptr_t kResumeTrailStartExit = 0x58FAFF;
constexpr uintptr_t kResumeTrailCheckpoint = 0x5903B1;
// Milestone 17 wave 3: inline walks of the first remote and the Mayhem per-player counters (walk_sites.h, docs/SR2_NOTES.md, "Walks
// and the Mayhem counter (milestone 17 wave 3)"). Where the jump outs go on, the functions the thunks call, and the Mayhem level layout.
constexpr uintptr_t kFraudStartBlockBody = 0x64CCD9;     // the remote start block of Fraud, after its `cmp [esp+0x10], 0; je`
constexpr uintptr_t kFraudStartBlockExit = 0x64D359;     // where every exit of that block joins
constexpr uintptr_t kFraudStartBlockAfter = 0x64D360;    // after the join's `mov edx, [esp+0xA58]`
constexpr uintptr_t kResumeFraudRagdoll = 0x64E8F6;
constexpr uintptr_t kResumeSnatchCleanup = 0x656D53;
constexpr uintptr_t kMayhemBonusLoopTop = 0x66BAF2;
constexpr uintptr_t kMayhemBonusLoopAfter = 0x66BB96;
constexpr uintptr_t kHumanDamageMultiplier = 0x968690;   // stdcall(human, &float)
constexpr uintptr_t kRagdollEnd = 0x9ACC90;              // cdecl(human, 0)
constexpr uintptr_t kPointInDistrict = 0x4B23F0;         // cdecl(point, district), AL out
constexpr uintptr_t kRemoteStat = 0x6B4F20;              // ECX = &value, ESI = stat id; broadcasts the stat with kSendBroadcast
constexpr uintptr_t kMayhemBonus = 0x668030;             // ECX = player, cdecl(id, target, kind, amount)
constexpr uintptr_t kFraudTargetDistrictIndex = 0xE976C4;  // dword: index into the table below
constexpr uintptr_t kFraudDistrictTable = 0x140C718;       // dword[]: the district records Fraud waits for
constexpr uintptr_t kMayhemBonusEpsilon = 0xDD0584;      // float 0.01: how near 1.0 a district multiplier counts as 1.0
constexpr uint32_t kSnatchCleanupArg = 1;                // 0x9DE310 at the end of Snatch (the start passes kSnatchSetupBothArg)
constexpr uint32_t kSnatchCleanupLookFlag = 0;           // 0x9E0350 at the end of Snatch (the start passes 1)
constexpr uintptr_t kFraudBlockHumanSlot = 0x10;         // the Fraud start block's [esp + 0x10]: the remote it is about
constexpr uintptr_t kSnatchDelivererSlot = 0x8C;         // the delivery's [esp + 0x8C]: the player that delivers
constexpr uintptr_t kHumanRagdollFlagsOffset = 0x84;     // byte: bit 0 set while the ragdoll is still on
constexpr uintptr_t kMayhemBlocksOffset = 0xB4;          // level: the first player score block, kMayhemBlockSize bytes each
constexpr size_t kMayhemBlockSize = 0x240;
constexpr uintptr_t kMayhemPartnerBlockOffset = 0x2F4;   // level: the second score block (kMayhemBlocksOffset + kMayhemBlockSize)
constexpr uintptr_t kMayhemBlockTotalOffset = 0xC;       // block: the sum of every kind
constexpr uintptr_t kMayhemEntriesOffset = 0x538;        // level: the district entry pointers
constexpr size_t kMayhemEntryCount = 3;
constexpr uintptr_t kMayhemResetField = 0x570;           // level dword the init clears
constexpr uintptr_t kMayhemEntryId = 0x00;
constexpr uintptr_t kMayhemEntryTarget = 0x0C;
constexpr uintptr_t kMayhemEntryPerUnit = 0x50;          // int: the bonus for one destruction
constexpr uintptr_t kMayhemEntryMultiplier = 0x54;       // float
constexpr uintptr_t kMayhemEntryLocalCount = 0x5C;       // int: destructions by the local player
constexpr uintptr_t kMayhemEntryRemoteCount = 0x60;      // int: destructions by anyone else

}  // namespace sr2
