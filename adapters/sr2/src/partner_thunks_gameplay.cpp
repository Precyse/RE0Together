// Thunks of gameplay: going down, the team wipe and downed message, vehicles, the mission start block, the Lua party natives
// and the experimental thunks that wait for a live probe (customisation exit passengers: probe G).

#include "fpu_guard.h"
#include "guest_call.h"
#include "log.h"
#include "partner_scope.h"
#include "partner_thunks.h"
#include "remote_players.h"
#include "sr2_addresses.h"
#include "thunk_frame.h"

namespace {

using partner_sites::Thunk;

constexpr size_t kLuaNativeCount = 4;
constexpr size_t kLuaVehicleCombat = 0;
constexpr size_t kLuaMaxFollowers = 1;
constexpr size_t kLuaDismissAll = 2;
constexpr size_t kLuaMeleeOnly = 3;
constexpr size_t kFirstRemote = 0;

void* g_goDownOriginal = nullptr;
void* g_luaOriginals[kLuaNativeCount] = {};

const uintptr_t g_partnerGetter = sr2::kRemotePlayerGetter;
const uintptr_t g_vehicleExitAfterGetter = sr2::kVehicleExitAfterGetter;
const uintptr_t g_vehicleExitDone = sr2::kVehicleExitDone;
const uintptr_t g_teamWipeResume = sr2::kTeamWipeResume;

uint32_t read32(uintptr_t address) { return *reinterpret_cast<const uint32_t*>(address); }

bool isDown(uintptr_t human) { return remote_players::downedTimerOf(human) >= 0; }

bool isDownOrWaiting(uintptr_t human) {
    return isDown(human) || (guest_call::call(sr2::kWaitingToRespawn, {.esi = human}) & 0xFF) != 0;
}


// D1: a human goes down (revivable) when any remote is within revive range, not only the first.
void __cdecl wrapGoDown(GuestFrame* frame) {
    const bool revivable = partner_scope::anyRemoteHuman([&](uintptr_t) {
        return (guest_call::call(reinterpret_cast<uintptr_t>(g_goDownOriginal), frame->regs) & 0xFF) != 0;
    });
    frame->regs.eax = revivable ? 1 : 0;
}

// A1: the remote driving the vehicle, else the default partner.
uintptr_t __cdecl remoteDriving(uintptr_t vehicle) {
    if (!vehicle) return partner_scope::partnerHuman();
    const uint32_t handle = read32(vehicle + sr2::kObjectHandleOffset);
    remote_players::Snapshot snapshot;
    remote_players::take(snapshot);
    for (size_t i = 0; i < snapshot.pairing.count; ++i) {
        const uintptr_t human = snapshot.pairing.items[i].human;
        if (human && read32(human + sr2::kHumanDrivenVehicleOffset) == handle) return human;
    }
    return partner_scope::partnerHuman();
}

// A2-A7: the first remote getting into a vehicle, else the default partner.
uintptr_t __cdecl remoteEntering() {
    remote_players::Snapshot snapshot;
    remote_players::take(snapshot);
    for (size_t i = 0; i < snapshot.pairing.count; ++i) {
        const uintptr_t human = snapshot.pairing.items[i].human;
        if (human && read32(human + sr2::kHumanActionOffset) - sr2::kActionEnterVehicleFirst < sr2::kActionEnterVehicleCount) {
            return human;
        }
    }
    return partner_scope::partnerHuman();
}

// B1, B2: the block of the mission start that makes the partner leave its vehicle runs once per remote. The loop
// spans several thunk calls, so the override lives on the heap between them.
struct VehicleExitLoop {
    partner_rules::Remote remotes[partner_rules::kMaxWalked];
    size_t count;
    size_t next;
    partner_scope::Override* turn;
};

thread_local VehicleExitLoop t_exitLoop;

void endTurn() {
    delete t_exitLoop.turn;
    t_exitLoop.turn = nullptr;
}

void beginTurn(size_t index) {
    endTurn();
    const partner_rules::Remote& remote = t_exitLoop.remotes[index];
    t_exitLoop.turn = new partner_scope::Override({remote.human, remote.member});
    t_exitLoop.next = index + 1;
}

void __cdecl startVehicleExitLoop() {
    endTurn();
    remote_players::Snapshot snapshot;
    remote_players::take(snapshot);
    t_exitLoop.count = 0;
    for (size_t i = 0; i < snapshot.pairing.count; ++i) {
        if (snapshot.pairing.items[i].human) t_exitLoop.remotes[t_exitLoop.count++] = snapshot.pairing.items[i];
    }
    t_exitLoop.next = 0;
    if (t_exitLoop.count) beginTurn(kFirstRemote);
}

uint32_t __cdecl nextVehicleExitTurn() {
    if (t_exitLoop.next >= t_exitLoop.count) {
        endTurn();
        return 0;
    }
    beginTurn(t_exitLoop.next);
    return 1;
}

// S1-S4: the Lua party natives apply to every remote, not only the first.
void wrapEachNative(GuestFrame* frame, size_t native) {
    uint32_t result = 0;
    partner_scope::eachRemoteHuman([&](uintptr_t) {
        result = guest_call::call(reinterpret_cast<uintptr_t>(g_luaOriginals[native]), frame->regs, {frame->args[0]});
    });
    frame->regs.eax = result;
}

void __cdecl wrapVehicleCombat(GuestFrame* frame) { wrapEachNative(frame, kLuaVehicleCombat); }
void __cdecl wrapMaxFollowers(GuestFrame* frame) { wrapEachNative(frame, kLuaMaxFollowers); }
void __cdecl wrapDismissAll(GuestFrame* frame) { wrapEachNative(frame, kLuaDismissAll); }
void __cdecl wrapMeleeOnly(GuestFrame* frame) { wrapEachNative(frame, kLuaMeleeOnly); }

// D2: the team wipe check at a human's death. A remote's death kills a downed local when every other
// remote is down or waiting too; the local's death wipes when every remote is down or waiting. Returns the flag the
// game keeps in its frame.
uint32_t __cdecl teamWipe(uintptr_t dead) {
    FpuGuard guard;
    remote_players::Snapshot snapshot;
    remote_players::take(snapshot);
    const uintptr_t local = remote_players::localHuman();

    uint32_t wipe = 0;
    if (dead == local) {
        wipe = snapshot.pairing.count && partner_rules::othersAllDown(snapshot.pairing, 0, isDownOrWaiting);
    } else if (remote_players::isRemotePlayerHuman(dead) &&
               partner_rules::othersAllDown(snapshot.pairing, dead, isDownOrWaiting)) {
        if (isDown(local)) {
            guest_call::call(sr2::kKillLocalHuman, {.esi = local});
            wipe = 1;
        } else if (isDownOrWaiting(local)) {
            wipe = 1;
        }
    }
    return wipe;
}

// D3: the downed message of a remote counts as a team wipe only when every other remote is down.
uintptr_t __cdecl remoteIfTeamDown(uintptr_t human) {
    FpuGuard guard;
    remote_players::Snapshot snapshot;
    remote_players::take(snapshot);

    return remote_players::isRemotePlayerHuman(human) && partner_rules::othersAllDown(snapshot.pairing, human, isDown) ? human : 0;
}

// B3 (probe G): 1 when the passenger is a remote player human.
uint32_t __cdecl isRemoteHuman(uintptr_t human) { return remote_players::isRemotePlayerHuman(human) ? 1 : 0; }

// B4 (probe G): every remote riding in the local player's vehicle is teleported out of the customised car. Logs where
// each remote ends up.
void __cdecl teleportPassengers(const uint32_t* args) {
    FpuGuard guard;
    const uintptr_t local = remote_players::localHuman();
    const uint32_t vehicle = read32(local + sr2::kHumanVehicleOffset);
    remote_players::Snapshot snapshot;
    remote_players::take(snapshot);
    for (size_t i = 0; i < snapshot.pairing.count; ++i) {
        const uintptr_t human = snapshot.pairing.items[i].human;
        if (!human || read32(human + sr2::kHumanVehicleOffset) != vehicle) continue;
        guest_call::call(sr2::kTeleportHuman, {.ecx = human}, {args[0], args[1], args[2], args[3], args[4], args[5]});
        const partner_rules::Vec3 at = remote_players::positionOf(human);
        logger::write("trace: probe G: remote 0x%X at %.1f %.1f %.1f after the customisation exit teleport",
                      static_cast<unsigned>(human), at.x, at.y, at.z);
    }
}

PARTNER_WRAP_STUB(goDownStub, wrapGoDown, 0)
PARTNER_WRAP_STUB(luaVehicleCombatStub, wrapVehicleCombat, 0)
PARTNER_WRAP_STUB(luaMaxFollowersStub, wrapMaxFollowers, 0)
PARTNER_WRAP_STUB(luaDismissAllStub, wrapDismissAll, 0)
PARTNER_WRAP_STUB(luaMeleeOnlyStub, wrapMeleeOnly, 0)
PARTNER_REG_STUB(remoteDrivingStub, esi, remoteDriving)
PARTNER_REG_STUB(remoteIfTeamDownStub, edi, remoteIfTeamDown)

// Replaces "xor bl, bl" and the getter call that start the block: the first remote takes its turn, then the two
// replaced instructions run here and the block goes on after them.
void __declspec(naked) vehicleExitEnterStub() {
    __asm {
        call startVehicleExitLoop
        xor bl, bl
        call dword ptr [g_partnerGetter]
        jmp dword ptr [g_vehicleExitAfterGetter]
    }
}

// Replaces the load that ends the block: the next remote takes its turn and the block runs again, or the block is done.
void __declspec(naked) vehicleExitNextStub() {
    __asm {
        call nextVehicleExitTurn
        test eax, eax
        jz finished
        xor bl, bl
        call dword ptr [g_partnerGetter]
        jmp dword ptr [g_vehicleExitAfterGetter]
    finished:
        mov edx, dword ptr ds:[0x33D630C]
        jmp dword ptr [g_vehicleExitDone]
    }
}

// pushad and pushfd put 0x24 bytes between esp and the game's frame, which holds the flag at +0x13.
void __declspec(naked) teamWipeStub() {
    __asm {
        pushad
        pushfd
        push ebx
        call teamWipe
        add esp, 4
        mov [esp + 0x24 + 0x13], al
        popfd
        popad
        jmp dword ptr [g_teamWipeResume]
    }
}

// The caller compares esi with the eax of the inline getter loop: a remote passenger counts as the partner.
void __declspec(naked) passengerIsRemoteStub() {
    __asm {
        push eax
        push esi
        call isRemoteHuman
        add esp, 4
        test eax, eax
        pop eax
        jz keep
        mov eax, esi
    keep:
        cmp esi, eax
        mov edi, [esp + 0x20]
        ret
    }
}

// The six arguments of 0x9DB480 are on the stack above the return address; the caller pops them.
void __declspec(naked) teleportPassengersStub() {
    __asm {
        lea eax, [esp + 4]
        push eax
        call teleportPassengers
        add esp, 4
        ret
    }
}

}  // namespace

namespace partner_thunks {

Binding bindGameplay(Thunk thunk) {
    switch (thunk) {
        case Thunk::WrapAnyGoDown: return {reinterpret_cast<void*>(&goDownStub), &g_goDownOriginal};
        case Thunk::RemoteDrivingVehicle: return {reinterpret_cast<void*>(&remoteDrivingStub), nullptr};
        case Thunk::RemoteEnteringVehicle: return {reinterpret_cast<void*>(&remoteEntering), nullptr};
        case Thunk::VehicleExitEnter: return {reinterpret_cast<void*>(&vehicleExitEnterStub), nullptr};
        case Thunk::VehicleExitNext: return {reinterpret_cast<void*>(&vehicleExitNextStub), nullptr};
        case Thunk::WrapEachVehicleCombat: return {reinterpret_cast<void*>(&luaVehicleCombatStub), &g_luaOriginals[kLuaVehicleCombat]};
        case Thunk::WrapEachMaxFollowers: return {reinterpret_cast<void*>(&luaMaxFollowersStub), &g_luaOriginals[kLuaMaxFollowers]};
        case Thunk::WrapEachDismissAll: return {reinterpret_cast<void*>(&luaDismissAllStub), &g_luaOriginals[kLuaDismissAll]};
        case Thunk::WrapEachMeleeOnly: return {reinterpret_cast<void*>(&luaMeleeOnlyStub), &g_luaOriginals[kLuaMeleeOnly]};
        case Thunk::TeamWipe: return {reinterpret_cast<void*>(&teamWipeStub), nullptr};
        case Thunk::RemoteIfTeamDown: return {reinterpret_cast<void*>(&remoteIfTeamDownStub), nullptr};
        case Thunk::PassengerIsRemote: return {reinterpret_cast<void*>(&passengerIsRemoteStub), nullptr};
        case Thunk::TeleportRemotePassengers: return {reinterpret_cast<void*>(&teleportPassengersStub), nullptr};
        default: return {nullptr, nullptr};
    }
}

}  // namespace partner_thunks
