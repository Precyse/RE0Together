// Thunks of the NPC AI tests (milestone 16 wave 2): what an NPC sees, how near it acts, which team a spawn record or an attacker is
// on, and which target is a follower of a remote player. Each asks every remote where the game asked the one default partner. With
// fewer than two remotes, or a pairing that cannot be trusted, each does what the replaced call did.

#include <windows.h>

#include <mutex>

#include "fpu_guard.h"
#include "guest_call.h"
#include "log.h"
#include "memory.h"
#include "partner_identity.h"
#include "partner_scope.h"
#include "party_ai_rules.h"
#include "party_ai_thunks.h"
#include "rate_limit.h"
#include "remote_players.h"
#include "squared_distance.h"
#include "sr2_addresses.h"
#include "thunk_frame.h"

namespace {

using partner_sites::Thunk;
using party_ai_rules::AiCounters;
using party_ai_rules::Pair;

constexpr uint64_t kSummaryIntervalMs = 60 * 1000;

std::mutex g_countLock;
AiCounters g_counters;
rate_limit::RateLimit g_summaryLimit(kSummaryIntervalMs);
rate_limit::OncePerKey g_teamBytesSeen;  // the remotes whose team byte was logged

static_assert(sr2::kObjectPositionOffset == 0x14, "the nearest remote stubs assemble this offset");

// The remote player humans now, and whether a thunk takes the shipped path.
class Remotes {
public:
    Remotes() {
        remote_players::take(snapshot_);
        humans_ = party_ai_rules::remoteHumansOf(snapshot_.pairing);
        shipped_ = party_ai_rules::shippedPath(snapshot_.pairing.trusted, humans_.count);
    }

    const party_ai_rules::HumanList& humans() const { return humans_; }
    bool shipped() const { return shipped_; }
    uint8_t slotOf(uintptr_t human) const { return party_ai_rules::slotOfHuman(snapshot_.pairing, human); }

private:
    remote_players::Snapshot snapshot_;
    party_ai_rules::HumanList humans_;
    bool shipped_;
};

bool rayBlocked(uintptr_t player, uint32_t npc, uint32_t point) {
    return (guest_call::call(sr2::kRayBlocked, {.edi = npc, .eax = player}, {point}) & 0xFF) != 0;
}

bool inView(uintptr_t player, uint32_t npc) {
    return (guest_call::call(sr2::kInView, {.edi = npc, .esi = player}) & 0xFF) != 0;
}

bool sameLeaderTeam(uintptr_t human, uintptr_t other) {
    return (guest_call::call(sr2::kSameLeaderTeam, {.eax = human}, {other}) & 0xFF) != 0;
}

// AI1, AI2: nobody sees the NPC when the ray of every remote is blocked, not only the default partner's. eax is the default partner,
// edi the NPC and the stack argument the point; the caller pops it.
void __cdecl everyRemoteRayBlocked(GuestFrame* frame) {
    const uintptr_t partner = frame->regs.eax;
    bool blocked = rayBlocked(partner, frame->regs.edi, frame->args[0]);
    const Remotes remotes;
    if (!remotes.shipped()) {
        const bool shipped = blocked;
        for (const uintptr_t remote : remotes.humans().span()) {
            if (!blocked) break;
            if (remote != partner) blocked = rayBlocked(remote, frame->regs.edi, frame->args[0]);
        }
        party_ai_thunks::count(&AiCounters::unseen, blocked != shipped);
    }
    frame->regs.eax = blocked ? 1 : 0;
}

// AI6: the NPC faces or is in range of a player when any remote passes the test; esi is the default partner, edi the NPC.
void __cdecl anyRemoteInView(GuestFrame* frame) {
    const uintptr_t partner = frame->regs.esi;
    bool seen = inView(partner, frame->regs.edi);
    const Remotes remotes;
    if (!remotes.shipped()) {
        const bool shipped = seen;
        for (const uintptr_t remote : remotes.humans().span()) {
            if (seen) break;
            if (remote != partner) seen = inView(remote, frame->regs.edi);
        }
        party_ai_thunks::count(&AiCounters::view, seen != shipped);
    }
    frame->regs.eax = seen ? 1 : 0;
}

// AI3, AI4: the behaviour 3 target. While a handler answers for the member that asked (the rop_req_own of the sender scope) that
// member; otherwise the remote nearest to the actor (the game tests for an actor of 0 itself, after the call).
uintptr_t __cdecl scopedOrNearestRemote(uintptr_t actor) {
    const Remotes remotes;
    if (remotes.shipped() || !actor) return partner_scope::partnerHuman();
    const bool scoped = partner_scope::overrideActive();
    party_ai_thunks::count(&AiCounters::behaviour3, scoped);
    return scoped ? partner_scope::partnerHuman() : partner_identity::nearest(static_cast<uint32_t>(actor + sr2::kObjectPositionOffset));
}

// AI5: the NPC acts while a remote is in range; the remote nearest to the NPC at `point` stands for them all.
uintptr_t __cdecl nearestRemoteToNpc(uint32_t point) {
    const Remotes remotes;
    if (remotes.shipped()) return partner_scope::partnerHuman();
    const uintptr_t nearest = partner_identity::nearest(point);
    party_ai_thunks::count(&AiCounters::range, nearest != partner_scope::partnerHuman());
    return nearest;
}

// AI7: the squared distance an ambient reaction measures, the shortest to any remote. `npcPosition` and `partnerPosition` are the
// vec3 pointers the call got; the float goes to `out` and the stub puts it on the x87 stack once the guard is gone.
void __cdecl nearestRemoteDistance(uint32_t npcPosition, uint32_t partnerPosition, float* out) {
    FpuGuard guard;
    float nearest = squared_distance::between(sr2::kSquaredDistance, npcPosition, partnerPosition);
    const Remotes remotes;
    if (!remotes.shipped()) {
        const float shipped = nearest;
        for (const uintptr_t remote : remotes.humans().span()) {
            const uint32_t position = static_cast<uint32_t>(remote + sr2::kObjectPositionOffset);
            if (position == partnerPosition) continue;
            const float distance = squared_distance::between(sr2::kSquaredDistance, npcPosition, position);
            if (distance < nearest) nearest = distance;
        }
        party_ai_thunks::count(&AiCounters::reactions, nearest != shipped);
    }
    *out = nearest;
}

void logTeamBytesOnce(const Remotes& remotes) {
    FpuGuard guard;
    for (const uintptr_t remote : remotes.humans().span()) {
        if (!g_teamBytesSeen.firstTime(remote)) continue;
        logger::write("trace: npc ai: remote slot %u team byte %d", remotes.slotOf(remote),
                      static_cast<int8_t>(byteField(remote, sr2::kHumanTeamByteOffset)));
    }
}

// AI8: a spawn record of a team passes when a player's team byte is that team; the first remote with the team byte of the record
// at `record`, else the default partner.
uintptr_t __cdecl remoteOfRecordTeam(uintptr_t record) {
    const uintptr_t partner = partner_scope::partnerHuman();
    const Remotes remotes;
    if (remotes.shipped()) return partner;
    if (party_ai_thunks::traceOn()) logTeamBytesOnce(remotes);
    const int8_t team = party_ai_rules::recordTeam(byteField(record, sr2::kSpawnRecordTeamOffset));
    uintptr_t answer = partner;
    for (const uintptr_t remote : remotes.humans().span()) {
        if (static_cast<int8_t>(byteField(remote, sr2::kHumanTeamByteOffset)) == team) {
            answer = remote;
            break;
        }
    }
    party_ai_thunks::count(&AiCounters::spawnTeam, answer != partner);
    return answer;
}

// AI9: an attacker on the team of a remote is not scaled; the first remote whose team leader is the attacker's, else the default
// partner.
uintptr_t __cdecl remoteOnTeamOfAttacker(uintptr_t attacker) {
    const uintptr_t partner = partner_scope::partnerHuman();
    const Remotes remotes;
    if (remotes.shipped()) return partner;
    uintptr_t answer = partner;
    for (const uintptr_t remote : remotes.humans().span()) {
        if (sameLeaderTeam(attacker, remote)) {
            answer = remote;
            break;
        }
    }
    party_ai_thunks::count(&AiCounters::damageTeam, answer != partner);
    return answer;
}

// AI10, AI11: both tests of a target's leader compare `[target + 0x2A4]` with the handle of the human returned: it is the target's
// leader itself when that is a remote player, so the test passes for a follower of any remote.
uintptr_t __cdecl remoteLeaderOfTarget(uintptr_t target) {
    const uintptr_t answer = partner_identity::match(remote_players::objectOfHandle(dwordField(target, sr2::kHumanLeaderOffset)));
    if (party_ai_thunks::traceOn()) {
        const Remotes remotes;
        if (!remotes.shipped()) party_ai_thunks::count(&AiCounters::followerTarget, answer != partner_scope::partnerHuman());
    }
    return answer;
}

PARTNER_WRAP_STUB(everyRemoteRayBlockedStub, everyRemoteRayBlocked, 0)
PARTNER_WRAP_STUB(anyRemoteInViewStub, anyRemoteInView, 0)
PARTNER_REG_STUB(remoteOfRecordTeamStub, esi, remoteOfRecordTeam)
PARTNER_REG_STUB(remoteOnTeamOfAttackerStub, ebp, remoteOnTeamOfAttacker)
PARTNER_REG_STUB(remoteLeaderOfTargetStub, esi, remoteLeaderOfTarget)
PARTNER_REG_STUB(scopedOrNearestRemoteStub, edi, scopedOrNearestRemote)

// ebp + 0x14 is the vec3 inside the NPC.
void __declspec(naked) nearestRemoteEbp14Stub() {
    __asm {
        lea eax, [ebp + 0x14]
        push eax
        call nearestRemoteToNpc
        add esp, 4
        ret
    }
}

// Replaces `call 0xBDEA30` (eax and ecx the two vec3 pointers, the float in ST0). The game's function leaves eax, ecx and edx as
// they were, so the stub does too.
void __declspec(naked) nearestRemoteDistanceStub() {
    __asm {
        push edx
        push ecx
        push eax
        sub esp, 4
        push esp
        push ecx
        push eax
        call nearestRemoteDistance
        add esp, 12
        fld dword ptr [esp]
        add esp, 4
        pop eax
        pop ecx
        pop edx
        ret
    }
}

}  // namespace

namespace party_ai_thunks {

void count(Pair AiCounters::*which, bool differs) {
    if (!traceOn()) return;
    FpuGuard guard;
    std::lock_guard<std::mutex> lock(g_countLock);
    Pair& pair = g_counters.*which;
    ++pair.evaluated;
    if (differs) ++pair.other;
    uint32_t triggers = 0;
    if (g_summaryLimit.allow(GetTickCount64(), triggers)) logger::write("%s", party_ai_rules::aiSummaryLine(g_counters).c_str());
}

partner_thunks::Binding bindNpcAi(Thunk thunk) {
    switch (thunk) {
        case Thunk::EveryRemoteRayBlocked: return {reinterpret_cast<void*>(&everyRemoteRayBlockedStub), nullptr};
        case Thunk::ScopedOrNearestRemoteEdi14: return {reinterpret_cast<void*>(&scopedOrNearestRemoteStub), nullptr};
        case Thunk::NearestRemoteEbp14: return {reinterpret_cast<void*>(&nearestRemoteEbp14Stub), nullptr};
        case Thunk::AnyRemoteInView: return {reinterpret_cast<void*>(&anyRemoteInViewStub), nullptr};
        case Thunk::NearestRemoteSquaredDistance: return {reinterpret_cast<void*>(&nearestRemoteDistanceStub), nullptr};
        case Thunk::RemoteOfRecordTeamEsi: return {reinterpret_cast<void*>(&remoteOfRecordTeamStub), nullptr};
        case Thunk::RemoteOnTeamOfEbp: return {reinterpret_cast<void*>(&remoteOnTeamOfAttackerStub), nullptr};
        case Thunk::RemoteLeaderOfEsi: return {reinterpret_cast<void*>(&remoteLeaderOfTargetStub), nullptr};
        default: return {nullptr, nullptr};
    }
}

}  // namespace party_ai_thunks
