#include "mission_players.h"

#include <cstring>

#include "fpu_guard.h"
#include "guest_call.h"
#include "memory.h"
#include "partner_scope.h"
#include "remote_players.h"
#include "spread_rules.h"
#include "sr2_addresses.h"
#include "thunk_frame.h"
#include "vehicle_seating_state.h"

namespace {

using partner_sites::Thunk;

constexpr uintptr_t kCutsceneWideHeaderFlag = 0xE8BC74;  // byte: the cutscn_pkt header is 4 bytes
constexpr size_t kHeaderBytes = 3;
constexpr size_t kWideHeaderBytes = 4;
constexpr size_t kEndPositionOffset = 1;  // after the sub id: three floats
constexpr size_t kEndForwardOffset = 0xD;  // and the forward vector
constexpr size_t kMaxMessageBytes = 0x200;
constexpr size_t kTeleportPayloadBytes = 0x36;
constexpr size_t kTeleportPositionOffset = 2;     // three floats, after the net id
constexpr size_t kTeleportOrientationOffset = 0xE;  // the orientation, row 0 first
constexpr size_t kBufferArg = 0;
constexpr size_t kLengthArg = 1;
constexpr size_t kContextArg = 1;

void* g_teleportOriginal = nullptr;

float readFloat(const uint8_t* at) {
    float value;
    memcpy(&value, at, sizeof(value));
    return value;
}

void addToFloat(uint8_t* at, float amount) {
    const float value = readFloat(at) + amount;
    memcpy(at, &value, sizeof(value));
}

// Moves the position at `position` by `rank` steps along the horizontal direction (x, z), keeping the height.
void spreadPosition(uint8_t* position, uint32_t rank, spread_rules::Offset direction) {
    const spread_rules::Offset step = spread_rules::stepsAlong(rank, direction.x, direction.z);
    addToFloat(position, step.x);
    addToFloat(position + 2 * sizeof(float), step.z);
}

// The session slots of all members and the host's.
struct Slots {
    uint8_t all[partner_rules::kMaxWalked];
    size_t count = 0;
    uint8_t host = 0;
};

Slots slotsOf(const remote_players::Snapshot& snapshot) {
    Slots slots;
    for (size_t i = 0; i < snapshot.memberCount; ++i) {
        slots.all[slots.count++] = snapshot.members[i].slot;
        if (snapshot.members[i].address == snapshot.hostMember) slots.host = snapshot.members[i].slot;
    }
    return slots;
}

uint32_t rankOf(const Slots& slots, uint8_t slot) { return spread_rules::guestRank(slot, slots.host, slots.all, slots.count); }

// MC3: replaces `call 0x814740` (edi = the session; the packet and its length pushed, which the caller pops) that broadcasts
// the end position of a cutscene: each remote member gets the position moved by its guest rank.
void __cdecl sendCutsceneEndPerRemote(GuestFrame* frame) {
    FpuGuard guard;
    const uint8_t* packet = reinterpret_cast<const uint8_t*>(frame->args[kBufferArg]);
    const size_t length = frame->args[kLengthArg];
    if (length > kMaxMessageBytes) return;
    remote_players::Snapshot snapshot;
    remote_players::take(snapshot);
    const Slots slots = slotsOf(snapshot);
    const size_t sub = *reinterpret_cast<const uint8_t*>(kCutsceneWideHeaderFlag) ? kWideHeaderBytes : kHeaderBytes;
    for (size_t i = 0; i < snapshot.pairing.count; ++i) {
        const partner_rules::Remote& remote = snapshot.pairing.items[i];
        if (!remote.member) continue;
        uint8_t copy[kMaxMessageBytes];
        memcpy(copy, packet, length);
        const spread_rules::Offset right = spread_rules::rightOf(readFloat(copy + sub + kEndForwardOffset),
                                                                 readFloat(copy + sub + kEndForwardOffset + 2 * sizeof(float)));
        spreadPosition(copy + sub + kEndPositionOffset, rankOf(slots, remote.slot), right);
        guest_call::call(sr2::kSendToMember, {.esi = static_cast<uint32_t>(remote.member)},
                         {static_cast<uint32_t>(reinterpret_cast<uintptr_t>(copy)), static_cast<uint32_t>(length), 0, 0});
    }
}

// MC4: replaces the partner getter before `cmp esi, [eax + 0x18D0]` (esi = this machine's last vehicle): the remote in that
// vehicle with a lower slot than ours (the one that keeps it), else the default partner. The slot answered is remembered for SE2.
uintptr_t __cdecl sharedVehicleHolder(uint32_t vehicle) {
    remote_players::Snapshot snapshot;
    remote_players::take(snapshot);
    const Slots slots = slotsOf(snapshot);
    uint8_t localSlot = 0;
    for (size_t i = 0; i < snapshot.memberCount; ++i) {
        if (snapshot.members[i].local) localSlot = snapshot.members[i].slot;
    }
    for (size_t i = 0; i < snapshot.pairing.count; ++i) {
        const partner_rules::Remote& remote = snapshot.pairing.items[i];
        if (remote.human && remote.slot < localSlot && readDword(remote.human + sr2::kHumanDrivenVehicleOffset) == vehicle) {
            vehicle_seating_state::rememberCheckpointHolder(remote.slot);
            return remote.human;
        }
    }
    vehicle_seating_state::rememberCheckpointHolder(vehicle_seating_rules::kDefaultHolder);
    return partner_scope::partnerHuman();
}

// MC5: the teleport a script sends to a human of this machine (cdecl(payload, context)). A guest that is not the lowest
// moves a step apart; the host, the lowest guest and a player in a vehicle (the teleport would move the vehicle) do not.
void __cdecl spreadRemoteTeleport(GuestFrame* frame) {
    const uint32_t* args = frame->args;
    const uintptr_t original = reinterpret_cast<uintptr_t>(g_teleportOriginal);
    const uintptr_t local = remote_players::localHuman();
    uint16_t id = 0;
    readMemory(args[kBufferArg], &id, sizeof(id));
    uint32_t rank = 0;
    if (local && id == remote_players::netIdOf(local) && readDword(local + sr2::kHumanVehicleOffset) == 0) {
        remote_players::Snapshot snapshot;
        remote_players::takeMembers(snapshot);
        uint8_t localSlot = 0;
        for (size_t i = 0; i < snapshot.memberCount; ++i) {
            if (snapshot.members[i].local) localSlot = snapshot.members[i].slot;
        }
        rank = rankOf(slotsOf(snapshot), localSlot);
    }
    if (rank == 0) {
        frame->regs.eax = guest_call::call(original, frame->regs, {args[kBufferArg], args[kContextArg]});
        return;
    }
    FpuGuard guard;
    uint8_t copy[kTeleportPayloadBytes];
    readMemory(args[kBufferArg], copy, sizeof(copy));
    const uint8_t* right = copy + kTeleportOrientationOffset;
    spreadPosition(copy + kTeleportPositionOffset, rank, {readFloat(right), readFloat(right + 2 * sizeof(float))});
    frame->regs.eax = guest_call::call(original, frame->regs, {static_cast<uint32_t>(reinterpret_cast<uintptr_t>(copy)), args[kContextArg]});
}

PARTNER_WRAP_STUB(sendCutsceneEndStub, sendCutsceneEndPerRemote, 0)
PARTNER_REG_STUB(sharedVehicleHolderStub, esi, sharedVehicleHolder)
PARTNER_WRAP_STUB(spreadTeleportStub, spreadRemoteTeleport, 0)

}  // namespace

namespace mission_players {

partner_thunks::Binding bind(Thunk thunk) {
    switch (thunk) {
        case Thunk::SendCutsceneEndPerRemote: return {reinterpret_cast<void*>(&sendCutsceneEndStub), nullptr};
        case Thunk::SharedVehicleHolder: return {reinterpret_cast<void*>(&sharedVehicleHolderStub), nullptr};
        case Thunk::SpreadRemoteTeleport: return {reinterpret_cast<void*>(&spreadTeleportStub), &g_teleportOriginal};
        default: return {nullptr, nullptr};
    }
}

}  // namespace mission_players
