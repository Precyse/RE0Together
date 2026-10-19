// Thunks of the session layer: the two getters, chunk streaming and object hand-over, the join tick, a member leaving,
// the pause-menu Disconnect and the messages that name one partner.
#include "activity_rules.h"
#include "activity_state.h"
#include "fpu_guard.h"
#include "guest_call.h"
#include "handoff_rules.h"
#include "log.h"
#include "memory.h"
#include "partner_scope.h"
#include "partner_thunks.h"
#include "remote_players.h"
#include "session_kick_rules.h"
#include "sr2_addresses.h"
#include "thunk_frame.h"

namespace {

using partner_sites::Thunk;

void* g_chunkLoadedOriginal = nullptr;
void* g_wrapEachMemberOriginal = nullptr;

const uintptr_t g_leaveTeardown = sr2::kLeaveTeardown;
const uintptr_t g_leaveDone = sr2::kLeaveDone;

uint32_t read32(uintptr_t address) { return *reinterpret_cast<const uint32_t*>(address); }

// O1: the chunk is loaded around any remote, not just the first.
void __cdecl wrapChunkLoaded(GuestFrame* frame) {
    const bool loaded = partner_scope::anyRemoteHuman([&](uintptr_t) {
        return (guest_call::call(reinterpret_cast<uintptr_t>(g_chunkLoadedOriginal), frame->regs) & 0xFF) != 0;
    });
    frame->regs.eax = loaded ? 1 : 0;
}

// Whether the game's chunk query O1 (edi = the chunk id) says the chunk is loaded around this remote.
bool chunkLoadedAround(const partner_rules::Remote& remote, uint16_t chunk) {
    partner_scope::Override scope({remote.human, remote.member});
    return (guest_call::call(reinterpret_cast<uintptr_t>(g_chunkLoadedOriginal), {.edi = chunk}) & 0xFF) != 0;
}

// O3, W6a: the nearest remote member that has the object's chunk, else the nearest remote member, else the first member that
// is not the local one.
uintptr_t __cdecl handoffTarget(uintptr_t object) {
    FpuGuard guard;
    remote_players::Snapshot snapshot;
    remote_players::take(snapshot);
    const partner_rules::Vec3 at = remote_players::positionOf(object);
    handoff_rules::Candidate candidates[partner_rules::kMaxWalked];
    const partner_rules::Remote* remotes[partner_rules::kMaxWalked];
    size_t count = 0;
    for (size_t i = 0; i < snapshot.pairing.count; ++i) {
        const partner_rules::Remote& remote = snapshot.pairing.items[i];
        if (!remote.human || !remote.member) continue;
        const partner_rules::Vec3 there = remote_players::positionOf(remote.human);
        const float dx = there.x - at.x, dy = there.y - at.y, dz = there.z - at.z;
        candidates[count] = {remote.member, dx * dx + dy * dy + dz * dz};
        remotes[count++] = &remote;
    }
    handoff_rules::nearestFirst(candidates, count);
    uintptr_t firstOther = 0;
    for (size_t i = 0; i < snapshot.memberCount && !firstOther; ++i) {
        if (!snapshot.members[i].local) firstOther = snapshot.members[i].address;
    }
    const uint16_t chunk = static_cast<uint16_t>(read32(object + sr2::kObjectChunkOffset));
    return handoff_rules::target(candidates, count, chunk, [&](uintptr_t member) {
        for (size_t i = 0; i < count; ++i) {
            if (remotes[i]->member == member) return chunkLoadedAround(*remotes[i], chunk);
        }
        return false;
    }, firstOther);
}

// J1: 1 when there is no remote member or every remote member has a player human that is done joining.
uint32_t __cdecl allRemotesReady() {
    remote_players::Snapshot snapshot;
    remote_players::take(snapshot);
    return partner_rules::allReady(snapshot.pairing, [](uintptr_t human) { return !remote_players::isJoining(human); });
}

// J2: the member the waiting dialog names.
uintptr_t __cdecl firstJoiningMember() {
    remote_players::Snapshot snapshot;
    remote_players::take(snapshot);
    const size_t joining = partner_rules::firstJoining(snapshot.pairing, sr2::kMemberStateInGame);
    return joining == partner_rules::kNone ? partner_scope::partnerMember() : snapshot.pairing.items[joining].member;
}

// L1: the mission and the co-op session end only when the member that left was the last one in the game.
uint32_t __cdecl leaveEndsSession(uintptr_t leaving) {
    if (read32(leaving + sr2::kMemberStateOffset) != sr2::kMemberStateInGame) return 0;
    remote_players::Snapshot snapshot;
    remote_players::take(snapshot);
    return partner_rules::isLastInGame(snapshot.pairing, leaving, sr2::kMemberStateInGame);
}

// ebx is the member that left.
void __declspec(naked) lastInGameRemoteLeftThunk() {
    __asm {
        push ebx
        call leaveEndsSession
        add esp, 4
        test eax, eax
        jnz teardown
        jmp dword ptr [g_leaveDone]
    teardown:
        jmp dword ptr [g_leaveTeardown]
    }
}

// L2: the pause overlay's Disconnect drops the remote members that hold the pause, else every remote member (the pause byte has
// no remote bit when N4 is not installed, or the pause ended while the prompt was open). The session tick removes a member
// whose send flag is 0. The first target is returned for the game to clear; with no remote member the game's answer stands.
uintptr_t __cdecl disconnectPausing() {
    remote_players::Snapshot snapshot;
    remote_players::takeMembers(snapshot);
    const uint8_t mask = readByte(sr2::kPartnerPaused);
    const session_kick_rules::Targets targets = session_kick_rules::disconnectTargets(mask, snapshot.members, snapshot.memberCount);
    if (targets.count == 0) return partner_scope::partnerMember();
    for (size_t i = 1; i < targets.count; ++i) byteField(targets.entries[i].member, sr2::kMemberSendEnabledOffset) = 0;
    logger::write("disconnect: pause mask 0x%02X, slots %s disconnected (%s)", mask,
                  session_kick_rules::slotsText(targets.entries, targets.count).c_str(), targets.pausing ? "pausing" : "every remote");
    return targets.entries[0].member;
}

bool memberKnows(uintptr_t member, uint32_t handle) {
    const uint32_t count = read32(member + sr2::kMemberKnownCountOffset);
    const uint32_t* handles = reinterpret_cast<const uint32_t*>(member + sr2::kMemberKnownHandlesOffset);
    for (uint32_t i = 0; i < count; ++i) {
        if (handles[i] == handle) return true;
    }
    return false;
}

// K1, K2: a remote member's list of known object handles holds the handle.
uint32_t __cdecl knownByAnyRemote(uint32_t handle) {
    remote_players::Snapshot snapshot;
    remote_players::take(snapshot);
    for (size_t i = 0; i < snapshot.pairing.count; ++i) {
        const uintptr_t member = snapshot.pairing.items[i].member;
        if (member && memberKnows(member, handle)) return 1;
    }
    return 0;
}

// K3: the first remote member that knows the object, else the default member.
uintptr_t __cdecl memberKnowing(uintptr_t object) {
    const uint32_t handle = read32(object + sr2::kObjectHandleOffset);
    remote_players::Snapshot snapshot;
    remote_players::take(snapshot);
    for (size_t i = 0; i < snapshot.pairing.count; ++i) {
        const uintptr_t member = snapshot.pairing.items[i].member;
        if (member && memberKnows(member, handle)) return member;
    }
    return partner_scope::partnerMember();
}

// K4: the script packet goes to every remote member.
void __cdecl wrapEachMember(GuestFrame* frame) {
    partner_scope::eachRemoteMember([&](uintptr_t) {
        guest_call::call(reinterpret_cast<uintptr_t>(g_wrapEachMemberOriginal), frame->regs, {frame->args[0]});
    });
}

// K5: the race message goes to one member instead of every member: inside a handler the sender, an answer to an invite
// the member that invited, else the default partner.
void __cdecl sendToPartner(uint32_t buffer, uint32_t length) {
    uintptr_t member = 0;
    if (!partner_scope::overrideActive() && activity_rules::racePeerGetsReply(activity_state::raceRequestSub(buffer))) {
        member = activity_state::takeRacePeer();
    }
    if (!member) member = partner_scope::partnerMember();
    if (member) guest_call::call(sr2::kSendToMember, {.esi = member}, {buffer, length, 0, 0});
}

// V1: the remote standing on the mover, else the default partner.
uintptr_t __cdecl remoteOnObject(uintptr_t object) {
    const uint32_t handle = read32(object + sr2::kObjectHandleOffset);
    remote_players::Snapshot snapshot;
    remote_players::take(snapshot);
    for (size_t i = 0; i < snapshot.pairing.count; ++i) {
        const uintptr_t human = snapshot.pairing.items[i].human;
        if (human && read32(human + sr2::kHumanMoverOffset) == handle) return human;
    }
    return partner_scope::partnerHuman();
}

PARTNER_WRAP_STUB(chunkLoadedStub, wrapChunkLoaded, 0)
PARTNER_WRAP_STUB(wrapEachMemberStub, wrapEachMember, 4)
PARTNER_REG_STUB(knownByAnyRemoteStub, esi, knownByAnyRemote)
PARTNER_REG_STUB(memberKnowingStub, edi, memberKnowing)

// The packet and its length were pushed for 0x814740; the caller pops them.
void __declspec(naked) sendToPartnerStub() {
    __asm {
        push dword ptr [esp + 8]
        push dword ptr [esp + 8]
        call sendToPartner
        add esp, 8
        ret
    }
}

// [esp + 0x8C] is [esp + 0x88] of the mover send, which the call pushed past.
void __declspec(naked) remoteOnObjectStub() {
    __asm {
        mov eax, [esp + 0x8C]
        push eax
        call remoteOnObject
        add esp, 4
        ret
    }
}

// The object being handed over is the first argument of 0x806910, [esp + 0x14] there and [esp + 0x18] past the call.
void __declspec(naked) hostHandoffTargetStub() {
    __asm {
        mov eax, [esp + 0x18]
        push eax
        call handoffTarget
        add esp, 4
        mov edi, eax
        ret
    }
}

}  // namespace

namespace partner_thunks {

Binding bindSession(Thunk thunk) {
    switch (thunk) {
        case Thunk::PartnerGetter: {
            const partner_scope::GetterHook hook = partner_scope::humanHook();
            return {hook.detour, hook.original};
        }
        case Thunk::PartnerMember: {
            const partner_scope::GetterHook hook = partner_scope::memberHook();
            return {hook.detour, hook.original};
        }
        case Thunk::AnyRemoteHasChunkLoaded: return {reinterpret_cast<void*>(&chunkLoadedStub), &g_chunkLoadedOriginal};
        case Thunk::WrapEachMember: return {reinterpret_cast<void*>(&wrapEachMemberStub), &g_wrapEachMemberOriginal};
        case Thunk::HostHandoffTarget: return {reinterpret_cast<void*>(&hostHandoffTargetStub), nullptr};
        case Thunk::AllRemotesReady: return {reinterpret_cast<void*>(&allRemotesReady), nullptr};
        case Thunk::FirstJoiningMember: return {reinterpret_cast<void*>(&firstJoiningMember), nullptr};
        case Thunk::LastInGameRemoteLeft: return {reinterpret_cast<void*>(&lastInGameRemoteLeftThunk), nullptr};
        case Thunk::DisconnectOthersReturnPartner: return {reinterpret_cast<void*>(&disconnectPausing), nullptr};
        case Thunk::AnyRemoteMemberKnows: return {reinterpret_cast<void*>(&knownByAnyRemoteStub), nullptr};
        case Thunk::MemberKnowingHandleEdi: return {reinterpret_cast<void*>(&memberKnowingStub), nullptr};
        case Thunk::SendToPartnerMember: return {reinterpret_cast<void*>(&sendToPartnerStub), nullptr};
        case Thunk::RemoteOnObject: return {reinterpret_cast<void*>(&remoteOnObjectStub), nullptr};
        default: return {nullptr, nullptr};
    }
}

}  // namespace partner_thunks
