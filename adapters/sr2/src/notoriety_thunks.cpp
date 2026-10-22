#include "notoriety_thunks.h"

#include <mutex>

#include "guest_call.h"
#include "log.h"
#include "memory.h"
#include "notoriety_rules.h"
#include "partner_scope.h"
#include "remote_players.h"
#include "sender_scope.h"
#include "sr2_addresses.h"
#include "thunk_frame.h"

namespace {

using notoriety_rules::LockKind;
using partner_sites::Thunk;

constexpr size_t kMaxRemotes = partner_rules::kMaxWalked;
constexpr uintptr_t kLockBytes = sizeof(int32_t);

bool g_trace = false;
std::mutex g_locksMutex;
notoriety_rules::GuestLocks g_locks;

bool teamSees(uint32_t team, uintptr_t human) {
    return (guest_call::call(sr2::kTeamSeesHuman, {.eax = team}, {static_cast<uint32_t>(human)}) & 0xFF) != 0;
}

// NW1 (eax = the team): the crime counts as seen when the team sees any remote. The game asked about the default partner only.
uint32_t __cdecl teamSeesAnyRemote(uint32_t team) {
    remote_players::Snapshot snapshot;
    remote_players::take(snapshot);
    const uintptr_t partner = partner_scope::partnerHuman();
    bool anyRemote = false;
    for (size_t i = 0; i < snapshot.pairing.count; ++i) {
        const partner_rules::Remote& remote = snapshot.pairing.items[i];
        if (!remote.human) continue;
        anyRemote = true;
        if (!teamSees(team, remote.human)) continue;
        if (g_trace && remote.human != partner) {
            logger::writeUnlessRepeated("trace: notoriety witness: team %u saw remote 0x%X slot %u", team, static_cast<unsigned>(remote.human),
                                        static_cast<unsigned>(remote.slot));
        }
        return 1;
    }
    return !anyRemote && partner && teamSees(team, partner) ? 1 : 0;
}

bool inAirOrWaterVehicle(uintptr_t human) {
    const uint32_t vehicle = guest_call::call(sr2::kVehicleObjectOf, {.ecx = readDword(human + sr2::kHumanVehicleOffset)});
    if (!vehicle) return false;
    for (const uintptr_t test : sr2::kVehicleClassTests) {
        if ((guest_call::call(test, {.edx = vehicle, .eax = vehicle}) & 0xFF) != 0) return true;
    }
    return false;
}

// NV1 (esi = the team): the human whose vehicle pauses the notoriety tick, the first remote in an air or water vehicle, else the
// default partner. The game then runs its own vehicle test on the answer.
uintptr_t __cdecl remoteInAirOrWater(uint32_t team) {
    remote_players::Snapshot snapshot;
    remote_players::take(snapshot);
    const uintptr_t partner = partner_scope::partnerHuman();
    for (size_t i = 0; i < snapshot.pairing.count; ++i) {
        const partner_rules::Remote& remote = snapshot.pairing.items[i];
        if (!remote.human || !inAirOrWaterVehicle(remote.human)) continue;
        if (g_trace && remote.human != partner) {
            logger::writeUnlessRepeated("trace: notoriety vehicle: remote 0x%X slot %u in an air or water vehicle holds team %u",
                                        static_cast<unsigned>(remote.human), static_cast<unsigned>(remote.slot), team);
        }
        return remote.human;
    }
    return partner;
}

// The slots of the members that are in the session now, the local one excluded.
size_t liveGuestSlots(uint8_t* out) {
    remote_players::Snapshot snapshot;
    remote_players::takeMembers(snapshot);
    size_t count = 0;
    for (size_t i = 0; i < snapshot.memberCount && count < kMaxRemotes; ++i) {
        if (!snapshot.members[i].local) out[count++] = snapshot.members[i].slot;
    }
    return count;
}

// The game's store of a guest lock: [table + team * stride + `offset` + index * 4] = value, kept for a message that names no sender or
// column.
int32_t& lockWord(uintptr_t offset, int32_t team, int32_t index) {
    const uintptr_t at = sr2::kNotorietyTable + offset + static_cast<uintptr_t>(team * static_cast<int32_t>(sr2::kNotorietyTeamStride) +
                                                                            index * static_cast<int32_t>(kLockBytes));
    return *reinterpret_cast<int32_t*>(at);
}

// NL1, NL2: a guest's lock goes into its own slot, and the game's array holds the merge of all guests' locks.
void storeGuestLock(LockKind kind, int32_t team, int32_t index, int32_t value) {
    const bool isMin = kind == LockKind::Min;
    int32_t& word = lockWord(isMin ? sr2::kNotorietyGuestMinOffset : sr2::kNotorietyGuestMaxOffset, team, index);
    const uintptr_t sender = sender_scope::currentSender();
    if (!sender || team < 0 || index < 0) {
        word = value;
        return;
    }
    uint8_t live[kMaxRemotes];
    const size_t liveCount = liveGuestSlots(live);
    const uint8_t slot = readByte(sender + sr2::kMemberSlotOffset);
    notoriety_rules::Stored stored;
    {
        std::lock_guard<std::mutex> lock(g_locksMutex);
        stored = g_locks.store(kind, static_cast<size_t>(team), static_cast<size_t>(index), slot, value, word, live, liveCount);
    }
    word = stored.merged;
    if (g_trace) {
        logger::write("trace: notoriety lock: team %d %s[%d] slot %u = %d, merged %d of %zu set%s", team, isMin ? "min" : "max", index,
                      static_cast<unsigned>(slot), value, stored.merged, stored.set, stored.columnReset ? " (column reset)" : "");
    }
}

void __cdecl guestMinLock(int32_t team, int32_t index, int32_t value) { storeGuestLock(LockKind::Min, team, index, value); }
void __cdecl guestMaxLock(int32_t team, int32_t index, int32_t value) { storeGuestLock(LockKind::Max, team, index, value); }

// The 0x55DEC0 call on the default partner: eax = the team and the partner is on the stack for the caller to pop.
void __declspec(naked) teamSeesAnyRemoteStub() {
    __asm {
        push eax
        call teamSeesAnyRemote
        add esp, 4
        ret
    }
}

PARTNER_REG_STUB(remoteInAirOrWaterStub, esi, remoteInAirOrWater)

// Replaces the min lock store (ebp = team, ecx = index, edx = value) and the max lock store (esi = team, edx = index, eax = value).
// The flags after both are dead.
void __declspec(naked) guestMinLockStub() {
    __asm {
        pushad
        push edx
        push ecx
        push ebp
        call guestMinLock
        add esp, 12
        popad
        ret
    }
}

void __declspec(naked) guestMaxLockStub() {
    __asm {
        pushad
        push eax
        push edx
        push esi
        call guestMaxLock
        add esp, 12
        popad
        ret
    }
}

}  // namespace

namespace notoriety_thunks {

void setTrace(bool trace) { g_trace = trace; }

partner_thunks::Binding bind(Thunk thunk) {
    switch (thunk) {
        case Thunk::NotorietyWitnessAnyRemote: return {reinterpret_cast<void*>(&teamSeesAnyRemoteStub), nullptr};
        case Thunk::NotorietyRemoteInAirOrWater: return {reinterpret_cast<void*>(&remoteInAirOrWaterStub), nullptr};
        case Thunk::NotorietyGuestMinLock: return {reinterpret_cast<void*>(&guestMinLockStub), nullptr};
        case Thunk::NotorietyGuestMaxLock: return {reinterpret_cast<void*>(&guestMaxLockStub), nullptr};
        default: return {nullptr, nullptr};
    }
}

}  // namespace notoriety_thunks
