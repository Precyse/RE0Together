// Snatch for every remote. The level keeps one carried count for "the partner" and its proximity tests read the partner cached
// at the start of the tick; here each remote has its own carried count and the proximity tests name the remote nearest to the
// group. Phase B adds the spawn point and the waypoint for every remote.
#include "activity_state.h"
#include "diversion_remotes.h"
#include "diversion_rules.h"
#include "diversion_stubs.h"
#include "diversion_swap.h"
#include "diversion_thunks.h"
#include "diversion_trace.h"
#include "fpu_guard.h"
#include "guest_call.h"
#include "log.h"
#include "memory.h"
#include "partner_identity.h"
#include "partner_scope.h"
#include "rate_limit.h"
#include "remote_players.h"
#include "sr2_addresses.h"
#include "thunk_frame.h"

namespace {

using partner_sites::Thunk;

constexpr uint32_t kZeroFlag = 0x40;
constexpr size_t kMinRemotesForPerRemoteSpawn = 2;

diversion_rules::CarryCounts g_carry;
rate_limit::RateLimit g_nearestLimit;
rate_limit::RateLimit g_waypointLimit;

void* g_spawnOriginal = nullptr;
void* g_waypointOriginal = nullptr;

int32_t& carryOf(uintptr_t level) { return reinterpret_cast<int32_t&>(dwordField(level, sr2::kSnatchCarryOffset)); }

// Which remote a human is. Outside a trusted pairing any remote player stands for the default partner, as in the shipped game.
struct RemoteRef {
    size_t slot;
    uintptr_t human;
    bool isDefault;
};

bool remoteRef(const diversion_remotes::View& view, uintptr_t human, RemoteRef& out) {
    if (!view.usable()) {
        out = {0, human, true};
        return remote_players::isRemotePlayerHuman(human);
    }
    const size_t at = view.indexOf(human);
    if (at == partner_rules::kNone) return false;
    out = {view.items()[at].slot, human, at == view.defaultIndex};
    return true;
}

// DVP9a: the count a human carries (the function 0x656D60 returns it for the partner): a remote's, else the game goes on.
void __cdecl carryOfEsi(BlockFrame* frame) {
    diversion_remotes::View view;
    diversion_remotes::take(view);
    RemoteRef ref;
    if (!remoteRef(view, frame->regs.esi, ref)) {
        frame->flags |= kZeroFlag;
        return;
    }
    frame->regs.eax = static_cast<uint32_t>(g_carry.get(ref.slot, ref.human, ref.isDefault, carryOf(*callerSlot(frame, sizeof(uint32_t)))));
    frame->flags &= ~kZeroFlag;
}

// DVP9b: a remote that picks up a ho carries one. The picker is the caller's argument at [esp + 0x10]; ecx holds it afterwards.
void __cdecl carrySet(BlockFrame* frame) {
    const uintptr_t picker = *callerSlot(frame, 0x10);
    frame->regs.ecx = static_cast<uint32_t>(picker);
    diversion_remotes::View view;
    diversion_remotes::take(view);
    RemoteRef ref;
    if (!remoteRef(view, picker, ref)) return;
    g_carry.set(ref.slot, ref.human, ref.isDefault, carryOf(frame->regs.ebx), 1);
    if (activity_state::tracing()) logger::write("trace: snatch: slot %u picked up, carry 1", static_cast<unsigned>(ref.slot));
}

// DVP9c: a delivery by a remote reduces its count (the caller's arguments: the player at [esp + 0x8C], the number at [esp + 0x88]).
void __cdecl carryDelivered(BlockFrame* frame) {
    const uint32_t player = *callerSlot(frame, 0x8C);
    const uint32_t delivered = *callerSlot(frame, 0x88);
    frame->regs.esi = player;
    frame->regs.edi = delivered;
    diversion_remotes::View view;
    diversion_remotes::take(view);
    RemoteRef ref;
    if (!remoteRef(view, player, ref)) return;
    int32_t& field = carryOf(frame->regs.ebp);
    g_carry.deliver(ref.slot, ref.human, ref.isDefault, field, static_cast<int32_t>(delivered));
    if (activity_state::tracing()) {
        logger::write("trace: snatch: slot %u delivered %u, carry %d", static_cast<unsigned>(ref.slot), delivered,
                      g_carry.get(ref.slot, ref.human, ref.isDefault, field));
    }
}

// DVP9d: a remote that is lost (esi) carries nothing.
void __cdecl carryClearOf(BlockFrame* frame) {
    diversion_remotes::View view;
    diversion_remotes::take(view);
    RemoteRef ref;
    if (remoteRef(view, frame->regs.esi, ref)) g_carry.set(ref.slot, ref.human, ref.isDefault, carryOf(frame->regs.ebx), 0);
}

// DVP9e: the level starts over.
void __cdecl carryClearAll(BlockFrame* frame) { g_carry.clearAll(carryOf(frame->regs.edi)); }

// DVP5a, DVP5b: the proximity tests of a ho group read the partner cached at the start of the tick; the remote nearest to the
// group is as near as "local or any remote" needs.
uintptr_t __cdecl nearestToPoint(uintptr_t point) {
    const uintptr_t nearest = partner_identity::nearest(static_cast<uint32_t>(point));
    if (diversion_trace::dueNow(g_nearestLimit)) {
        logger::write("trace: snatch: nearest remote 0x%X to group 0x%X", static_cast<unsigned>(nearest), static_cast<unsigned>(point));
    }
    return nearest;
}

uintptr_t __cdecl nearestToGroup(uintptr_t group) { return nearestToPoint(group + sr2::kSnatchGroupPointOffset); }

// The group object is in ecx; the caller's cached partner is its [esp + 0x24], and the flags are those of the test on it.
void __declspec(naked) nearestCachedPartnerEcx1cStub() {
    __asm {
        pushad
        push ecx
        call nearestToGroup
        add esp, 4
        mov [esp + 0x20 + 4 + 0x24], eax
        popad
        cmp dword ptr [esp + 0x28], 0
        ret
    }
}

// The point is in eax; the result is also the caller's ebp, and the flags are those of the test on it.
void __declspec(naked) nearestCachedPartnerEbpAtEaxStub() {
    __asm {
        push eax
        push ecx
        push edx
        push eax
        call nearestToPoint
        add esp, 4
        mov [esp + 12 + 4 + 0x24], eax
        mov ebp, eax
        pop edx
        pop ecx
        pop eax
        test ebp, ebp
        ret
    }
}

DIVERSION_BLOCK_STUB(carryOfEsiStub, carryOfEsi)
DIVERSION_BLOCK_STUB(carrySetStub, carrySet)
DIVERSION_BLOCK_STUB(carryDeliveredStub, carryDelivered)
DIVERSION_BLOCK_STUB(carryClearOfStub, carryClearOf)
DIVERSION_BLOCK_STUB(carryClearAllStub, carryClearAll)

// Phase B, DVP1: the ho spawns where it is hidden from the local player and one remote; with several remotes the answer for each
// is asked, and the one that is farthest from its nearest player is taken. Hiding it from the others waits for a live run.
void __cdecl spawnPerRemote(GuestFrame* frame) {
    const uint32_t level = frame->args[0];
    const uintptr_t original = reinterpret_cast<uintptr_t>(g_spawnOriginal);
    diversion_remotes::View view;
    diversion_remotes::take(view);
    size_t withHuman = 0;
    for (size_t i = 0; i < view.count(); ++i) withHuman += view.items()[i].human != 0;
    if (!view.usable() || withHuman < kMinRemotesForPerRemoteSpawn) {
        frame->regs.eax = guest_call::call(original, frame->regs, {level});
        return;
    }
    uint32_t answers[partner_rules::kMaxWalked];
    size_t answerCount = 0;
    for (size_t i = 0; i < view.count(); ++i) {
        const partner_rules::Remote& remote = view.items()[i];
        if (!remote.human) continue;
        partner_scope::Override turn({remote.human, remote.member});
        const uint32_t answer = guest_call::call(original, frame->regs, {level});
        if (answer) answers[answerCount++] = answer;
    }
    partner_rules::Vec3 candidates[partner_rules::kMaxWalked];
    partner_rules::Vec3 players[partner_rules::kMaxWalked + 1];
    size_t playerCount = 0;
    {
        FpuGuard guard;
        for (size_t i = 0; i < answerCount; ++i) candidates[i] = diversion_remotes::vec3At(answers[i] + sr2::kSnatchGroupPointOffset);
        const uintptr_t local = remote_players::localHuman();
        if (local) players[playerCount++] = remote_players::positionOf(local);
        for (size_t i = 0; i < view.count(); ++i) {
            if (view.items()[i].human) players[playerCount++] = remote_players::positionOf(view.items()[i].human);
        }
        const size_t best = diversion_rules::bestSpawn(candidates, answerCount, players, playerCount);
        frame->regs.eax = best == partner_rules::kNone ? 0 : answers[best];
    }
    if (activity_state::tracing()) {
        logger::write("trace: snatch: spawn answers %u, chose 0x%X", static_cast<unsigned>(answerCount), frame->regs.eax);
    }
}

// Phase B, DVP7: the waypoint of the remote side is the game's index 1 fields of the level (the target group and the last
// target); the game's function runs once per remote with that remote's pair.
struct Waypoint {
    uint32_t group;
    uint32_t last;
};

struct WaypointAccess {
    uintptr_t level;
    Waypoint read() const { return {readDword(level + sr2::kSnatchWaypointGroupOffset), readDword(level + sr2::kSnatchWaypointLastOffset)}; }
    void write(const Waypoint& waypoint) const {
        dwordField(level, sr2::kSnatchWaypointGroupOffset) = waypoint.group;
        dwordField(level, sr2::kSnatchWaypointLastOffset) = waypoint.last;
    }
};

diversion_rules::SwapTable<Waypoint> g_waypoints;
uintptr_t g_waypointLevel = 0;

void __cdecl waypointPerRemote(GuestFrame* frame) {
    const uint32_t level = frame->args[0];
    const uintptr_t original = reinterpret_cast<uintptr_t>(g_waypointOriginal);
    diversion_remotes::View view;
    diversion_remotes::take(view);
    size_t withHuman = 0;
    for (size_t i = 0; i < view.count(); ++i) withHuman += view.items()[i].human != 0;
    if (!view.usable() || withHuman < kMinRemotesForPerRemoteSpawn) {
        frame->regs.eax = guest_call::call(original, frame->regs, {level});
        return;
    }
    if (level != g_waypointLevel) {
        g_waypoints.forget();
        g_waypointLevel = level;
    }
    const WaypointAccess access{level};
    for (size_t i = 0; i < view.count(); ++i) {
        if (!view.items()[i].human) continue;
        diversion_swap::RemoteTurn<Waypoint, WaypointAccess> turn(g_waypoints, access, view, i);
        frame->regs.eax = guest_call::call(original, frame->regs, {level});
        if (diversion_trace::dueNow(g_waypointLimit)) logger::write("trace: snatch: waypoint for slot %u", view.items()[i].slot);
    }
}

PARTNER_WRAP_STUB(spawnPerRemoteStub, spawnPerRemote, 4)
PARTNER_WRAP_STUB(waypointPerRemoteStub, waypointPerRemote, 4)

}  // namespace

namespace diversion_thunks {

partner_thunks::Binding bindSnatch(Thunk thunk) {
    switch (thunk) {
        case Thunk::NearestCachedPartnerEcx1c: return {reinterpret_cast<void*>(&nearestCachedPartnerEcx1cStub), nullptr};
        case Thunk::NearestCachedPartnerEbpAtEax: return {reinterpret_cast<void*>(&nearestCachedPartnerEbpAtEaxStub), nullptr};
        case Thunk::SnatchCarryOfEsi: return {reinterpret_cast<void*>(&carryOfEsiStub), nullptr};
        case Thunk::SnatchCarrySet: return {reinterpret_cast<void*>(&carrySetStub), nullptr};
        case Thunk::SnatchCarryDelivered: return {reinterpret_cast<void*>(&carryDeliveredStub), nullptr};
        case Thunk::SnatchCarryClearOf: return {reinterpret_cast<void*>(&carryClearOfStub), nullptr};
        case Thunk::SnatchCarryClearAll: return {reinterpret_cast<void*>(&carryClearAllStub), nullptr};
        case Thunk::SnatchSpawnPerRemote: return {reinterpret_cast<void*>(&spawnPerRemoteStub), &g_spawnOriginal};
        case Thunk::SnatchWaypointPerRemote: return {reinterpret_cast<void*>(&waypointPerRemoteStub), &g_waypointOriginal};
        default: return {nullptr, nullptr};
    }
}

}  // namespace diversion_thunks
