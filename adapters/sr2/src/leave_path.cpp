#include "leave_path.h"

#include <algorithm>
#include <vector>

#include "creation_queue.h"
#include "guest_call.h"
#include "hooks.h"
#include "leave_rules.h"
#include "log.h"
#include "memory.h"
#include "remote_players.h"
#include "sr2_addresses.h"

namespace {

constexpr size_t kMaxRecordsWalked = 8192;  // the list is circular; the walk stops here whatever it holds
constexpr uintptr_t kVehicleFlagsOffset = 0xCC;
constexpr uint32_t kVehicleOccupiedFlag = 0x10;
constexpr uintptr_t kPlayerFlagsOffset = 0xC0;
constexpr uint32_t kPlayerInVehicleFlag = 0x40000;
constexpr uintptr_t kPlayerVehicleOffset = 0x374;
constexpr uintptr_t kObjectDestroySlot = 0x10;  // vtable slot: destroy(0)
constexpr uintptr_t kObjectTableStride = 16;
constexpr uint32_t kHandleIndexMask = 0xFFF;

using LeaveFn = void(__cdecl*)(uint32_t slot);

bool g_trace = false;
LeaveFn g_original = nullptr;
std::vector<leave_rules::Item> g_renamed;  // LV2: the records this leave renames, with the id they had
uint32_t g_dropped = 0;                    // LV1: the leaver's records this leave dropped
const uintptr_t g_leaverOwned = sr2::kLeaveLeaverOwned;
const uintptr_t g_otherOwner = sr2::kLeaveOtherOwner;
const uintptr_t g_nextRecord = sr2::kLeaveNextRecord;
const uintptr_t g_renameBody = sr2::kLeaveRenameBody;
const uintptr_t g_renameSkip = sr2::kLeaveRenameSkip;
const uintptr_t g_cutsceneCleanup = sr2::kCutsceneCleanup;


uint8_t readByte(uintptr_t address) {
    uint8_t value = 0;
    readMemory(address, &value, sizeof(value));
    return value;
}

uint16_t readWord(uintptr_t address) {
    uint16_t value = 0;
    readMemory(address, &value, sizeof(value));
    return value;
}

// The session as this machine sees it: its members and their slots.
struct SessionView {
    uintptr_t session = 0;
    bool localIsHost = false;
    uint8_t localSlot = 0;
    uint8_t hostSlot = 0;
};

SessionView sessionView() {
    SessionView view;
    view.session = readDword(sr2::kSessionPointer);
    if (!view.session) return view;
    const uintptr_t local = readDword(view.session + sr2::kSessionLocalMemberOffset);
    const uintptr_t host = readDword(view.session + sr2::kSessionHostMemberOffset);
    view.localIsHost = local == host;
    view.localSlot = readByte(local + sr2::kMemberSlotOffset);
    view.hostSlot = readByte(host + sr2::kMemberSlotOffset);
    return view;
}

// The net records of the session, in list order.
std::vector<uintptr_t> recordList() {
    std::vector<uintptr_t> records;
    const uintptr_t head = readDword(sr2::kRecordListHead);
    uintptr_t record = head;
    for (size_t i = 0; record != 0 && i < kMaxRecordsWalked; ++i) {
        records.push_back(record);
        record = readDword(record + sr2::kRecordNextOffset);
        if (record == head) break;
    }
    return records;
}

bool contains(const std::vector<uintptr_t>& records, uintptr_t record) {
    return std::find(records.begin(), records.end(), record) != records.end();
}

// The object a handle names, or 0.
uintptr_t objectOf(uint32_t handle) {
    if (!handle) return 0;
    const uintptr_t object = readDword(sr2::kObjectTable + (handle & kHandleIndexMask) * kObjectTableStride);
    return object && readDword(object + sr2::kObjectHandleOffset) == handle ? object : 0;
}

// The local player gets out of the vehicle that is about to disappear, as the game's single player leave does.
void leaveVehicleOf(uintptr_t object) {
    const uint32_t vehicle = guest_call::call(sr2::kVehicleOfHandle, {.ecx = readDword(object + sr2::kObjectHandleOffset)});
    if (!vehicle || !(readDword(vehicle + kVehicleFlagsOffset) & kVehicleOccupiedFlag)) return;
    const uintptr_t player = readDword(sr2::kLocalPlayer);
    if (!player || !(readDword(player + kPlayerFlagsOffset) & kPlayerInVehicleFlag)) return;
    if (readDword(player + kPlayerVehicleOffset) != readDword(vehicle + sr2::kObjectHandleOffset)) return;
    guest_call::call(sr2::kExitVehicle, {}, {0, 0});
}

// LV1: a guest meets a record of the leaver. 1 = handled here, 0 = the game goes on with it.
uint32_t __cdecl dropLeaverRecord(uintptr_t record) {
    const SessionView view = sessionView();
    const uint8_t leaver = readByte(record + sr2::kRecordOwnerOffset);
    if (!view.session || !leave_rules::guestDrops(view.localIsHost, leaver, view.hostSlot)) return 0;
    const uint32_t handle = readDword(record + sr2::kRecordHandleOffset);
    if (handle && guest_call::call(sr2::kIsHumanHandle, {.ecx = handle})) return 1;  // the host's destroy removes players
    const uintptr_t object = objectOf(handle);
    if (object) leaveVehicleOf(object);
    guest_call::call(sr2::kUnlinkRecord, {.esi = static_cast<uint32_t>(record)});
    if (object) guest_call::call(readDword(readDword(object) + kObjectDestroySlot), {.ecx = static_cast<uint32_t>(object)}, {0});
    ++g_dropped;
    if (g_trace) logger::write("trace: leave: dropped record id 0x%04X of slot %u locally", handle & 0xFFFF, leaver);
    return 1;
}

// LV2: the game is about to rename `record`, whose id may be the leaver's (bx). 1 = rename it, 0 = leave it.
uint32_t __cdecl keepRename(uintptr_t record, uint32_t leaverSlot) {
    const uint16_t id = readWord(record + sr2::kRecordIdOffset);
    if (static_cast<uint32_t>(id >> sr2::kRecordIdSlotShift) != (leaverSlot & 0xFFFF)) return 0;
    const SessionView view = sessionView();
    if (!view.session ||
        !leave_rules::keepsRename(readByte(record + sr2::kRecordOwnerOffset), view.localSlot, static_cast<uint8_t>(leaverSlot),
                                  view.localIsHost)) {
        return 0;
    }
    g_renamed.push_back({record, id});
    if (g_trace) logger::write("trace: leave: renaming record id 0x%04X", id);
    return 1;
}

// The members that get a reissued record.
std::vector<remote_players::MemberInfo> usableMembers(uint8_t leaver) {
    remote_players::Snapshot snapshot;
    remote_players::takeMembers(snapshot);
    std::vector<remote_players::MemberInfo> members;
    for (size_t i = 0; i < snapshot.memberCount; ++i) {
        const remote_players::MemberInfo& member = snapshot.members[i];
        if (leave_rules::usableMember(member.local, readByte(member.address + sr2::kMemberSendEnabledOffset), member.state,
                                      member.slot, leaver)) {
            members.push_back(member);
        }
    }
    return members;
}

// The destroy of a record under its old id, built as the game's own 0x8053B0 does, with the payload its class appends.
leave_rules::Message destroyMessageOf(const leave_rules::Item& item) {
    const uintptr_t session = readDword(sr2::kSessionPointer);
    const uint32_t recordClass = readDword(item.record + sr2::kRecordClassOffset);
    leave_rules::Message message = leave_rules::destroyMessage(readDword(session + sr2::kSessionValueOffset),
                                                               static_cast<uint8_t>(recordClass), item.oldId,
                                                               readByte(sr2::kDestroyWideHeaderFlag) != 0);
    uint32_t length = static_cast<uint32_t>(message.length);
    const uintptr_t classObject = readDword(sr2::kRecordClassTable + recordClass * sizeof(uint32_t));
    guest_call::call(readDword(readDword(classObject) + sr2::kClassDestroyPayloadSlot), {.ecx = static_cast<uint32_t>(classObject)},
                     {readDword(item.record + sr2::kRecordHandleOffset),
                      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(message.bytes.data())),
                      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&length))});
    message.length = length;
    return leave_rules::sealed(message);
}

// Every record of the set: destroy the old id on each member, then queue the creation.
void reissue(const leave_rules::ReissueSet& set, const std::vector<remote_players::MemberInfo>& members) {
    for (const leave_rules::Item& item : set.items()) {
        const leave_rules::Message message = destroyMessageOf(item);
        for (const remote_players::MemberInfo& member : members) {
            guest_call::call(sr2::kSendMessage, {.esi = static_cast<uint32_t>(member.address)},
                             {static_cast<uint32_t>(reinterpret_cast<uintptr_t>(message.bytes.data())),
                              static_cast<uint32_t>(message.length), 0, 0});
            const bool queued = creation_queue::enqueue(member.address, item.record);
            if (g_trace) {
                logger::write("trace: leave: reissued id 0x%04X to slot %u%s", item.oldId, member.slot,
                              queued ? "" : " (behind the full creation queue)");
            }

        }
    }
}

// LV3: around the leave function.
void __cdecl detourLeave(uint32_t slotArgument) {
    const uint8_t leaver = static_cast<uint8_t>(slotArgument);
    const SessionView view = sessionView();
    if (!view.session) {
        g_original(slotArgument);
        return;
    }
    std::vector<leave_rules::Item> owned;
    for (const uintptr_t record : recordList()) {
        if (readByte(record + sr2::kRecordOwnerOffset) == leaver) owned.push_back({record, readWord(record + sr2::kRecordIdOffset)});
    }
    g_renamed.clear();
    g_dropped = 0;
    g_original(slotArgument);

    const std::vector<uintptr_t> present = recordList();
    const auto ownedHere = [&](uintptr_t record) {
        return contains(present, record) && readByte(record + sr2::kRecordOwnerOffset) == view.localSlot;
    };
    leave_rules::ReissueSet set;
    size_t tookOver = 0;
    for (const leave_rules::Item& item : owned) {
        if (!ownedHere(item.record)) continue;
        set.add(item.record, item.oldId);
        ++tookOver;
    }
    for (const leave_rules::Item& item : g_renamed) {
        if (ownedHere(item.record)) set.add(item.record, item.oldId);
    }
    const auto members = usableMembers(leaver);
    reissue(set, members);
    if (!g_trace) return;
    if (view.localIsHost) {
        logger::write("trace: leave: slot %u, took over %zu, renamed %zu, reissued %zu to %zu member(s)", leaver, tookOver,
                      g_renamed.size(), set.items().size(), members.size());
    } else {
        logger::write("trace: leave: slot %u, dropped %u locally, renamed %zu, reissued %zu", leaver, g_dropped,
                      g_renamed.size(), set.items().size());
    }
}

// LV1: replaces `cmp byte [esi], cl; jne other` of the loop over the net records (esi = the record, cl = the leaver).
void __declspec(naked) dropLeaverRecordStub() {
    __asm {
        cmp byte ptr [esi], cl
        jne other
        pushad
        push esi
        call dropLeaverRecord
        add esp, 4
        mov dword ptr [esp + 28], eax
        popad
        test eax, eax
        jnz dropped
        jmp dword ptr [g_leaverOwned]
    dropped:
        jmp dword ptr [g_nextRecord]
    other:
        jmp dword ptr [g_otherOwner]
    }
}

// LV2: replaces the id test of the rename loop (esi = the record, bx = the leaver's slot).
void __declspec(naked) keepRenameStub() {
    __asm {
        pushad
        push ebx
        push esi
        call keepRename
        add esp, 8
        mov dword ptr [esp + 28], eax
        popad
        test eax, eax
        jz skip
        jmp dword ptr [g_renameBody]
    skip:
        jmp dword ptr [g_renameSkip]
    }
}

// N2a, N3: replaces `call 0x6C6B90` (the guest branch of the member left callback 0x8CB260, also reached with an idle
// session). ebx = the leaver, esi = the session. 1 = the cutscene cleanup the game had; 0 = done here.
uint32_t __cdecl guestMemberLeft(uintptr_t session, uintptr_t leaver) {
    const uint32_t state = readDword(session + sr2::kSessionBusyStateOffset);
    const leave_rules::GuestLeave action = leave_rules::guestLeave(
        state, leaver == readDword(session + sr2::kSessionHostMemberOffset), leaver == readDword(session + sr2::kSessionLocalMemberOffset));
    if (action == leave_rules::GuestLeave::CutsceneCleanup) return 1;
    if (action == leave_rules::GuestLeave::ObjectPass) {
        guest_call::call(sr2::kLeaveFunction, {}, {readByte(leaver + sr2::kMemberSlotOffset)});
    }
    return 0;
}

void __declspec(naked) guestMemberLeftStub() {
    __asm {
        pushad
        push ebx
        push esi
        call guestMemberLeft
        add esp, 8
        mov dword ptr [esp + 28], eax
        popad
        test eax, eax
        jnz cleanup
        ret
    cleanup:
        jmp dword ptr [g_cutsceneCleanup]
    }
}

}  // namespace

namespace leave_path {

partner_thunks::Binding bind(partner_sites::Thunk thunk) {
    if (thunk != partner_sites::Thunk::GuestMemberLeft) return {nullptr, nullptr};
    return {reinterpret_cast<void*>(&guestMemberLeftStub), nullptr};
}

Thunks thunks() {
    return {reinterpret_cast<uintptr_t>(&dropLeaverRecordStub), reinterpret_cast<uintptr_t>(&keepRenameStub)};
}

void setTrace(bool trace) { g_trace = trace; }

bool installHook() {
    return hooks::install("leave_reissue", sr2::kLeaveFunction, reinterpret_cast<void*>(&detourLeave),
                          reinterpret_cast<void**>(&g_original));
}

void removeHook() { hooks::remove(sr2::kLeaveFunction); }

}  // namespace leave_path
