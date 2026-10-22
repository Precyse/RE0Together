// Thunks of FUZZ (milestone 17 wave 3): the level, the crime weapons, the crime area, the cameraman, the crime targets, the crime
// vehicle and the riot know "the partner" and the local player only. Each runs for every remote where the game ran for the one default
// partner. With fewer than two remotes, or a pairing that cannot be trusted, each does what the replaced call did.
#include <cmath>
#include <cstring>

#include "diversion_stubs.h"
#include "diversion_trace.h"
#include "fpu_guard.h"
#include "fuzz_rules.h"
#include "fuzz_thunks.h"
#include "guest_call.h"
#include "log.h"
#include "memory.h"
#include "partner_identity.h"
#include "partner_scope.h"
#include "party_ai_remotes.h"
#include "rate_limit.h"
#include "squared_distance.h"
#include "sr2_addresses.h"
#include "thunk_frame.h"

namespace {

using party_ai_remotes::Remotes;
using partner_sites::Thunk;

// The spawn point stub assembles the slot plus the return address.
static_assert(sr2::kFuzzSpawnPointSlot + sizeof(uint32_t) == 0xC, "the spawn point stub assembles this offset");

void* g_riotNearOriginal = nullptr;
void* g_pointNearOriginal = nullptr;
uint32_t g_rollState = 0;

rate_limit::OncePerKey g_areaSeen;
rate_limit::OncePerKey g_vehicleSeen;
rate_limit::OncePerKey g_rioterSeen;
rate_limit::RateLimit g_fightLimit;
rate_limit::RateLimit g_targetLimit;
rate_limit::RateLimit g_riotLimit;
rate_limit::RateLimit g_pointLimit;
rate_limit::RateLimit g_spawnLimit;

bool tracing() { return activity_state::tracing(); }

// Calls `call(human, expanded)` for the human the game passed (the shipped path) or for every remote human; the last result is the
// function's result in eax. `expanded` is true for the remotes of a game with more than one.
template <typename Call>
void forEachRemote(GuestFrame* frame, uintptr_t given, Call call) {
    const Remotes remotes;
    if (remotes.shipped()) {
        frame->regs.eax = call(given, false);
        return;
    }
    for (const uintptr_t human : remotes.humans().span()) frame->regs.eax = call(human, true);
}

// FZ2, FZ3: the FUZZ mode byte of every remote (stdcall(human, const uint8_t* mode)).
void __cdecl eachRemoteFuzzMode(GuestFrame* frame) {
    forEachRemote(frame, frame->args[0], [&](uintptr_t human, bool expanded) {
        const uint32_t result = guest_call::call(sr2::kFuzzSetMode, {}, {static_cast<uint32_t>(human), frame->args[1]});
        if (expanded && tracing()) {
            logger::write("trace: fuzz: mode %u for remote 0x%X", readByte(frame->args[1]), static_cast<unsigned>(human));
        }
        return result;
    });
}

// FZ4, FZ7: the weapon goes to every remote (ecx = the human, cdecl with 5 arguments, the first is the weapon info).
void __cdecl eachRemoteGiveWeapon(GuestFrame* frame) {
    forEachRemote(frame, frame->regs.ecx, [&](uintptr_t human, bool expanded) {
        guest_call::Regs regs = frame->regs;
        regs.ecx = static_cast<uint32_t>(human);
        const uint32_t result = guest_call::call(sr2::kGiveWeapon, regs, {frame->args[0], frame->args[1], frame->args[2], frame->args[3], frame->args[4]});
        if (expanded && tracing()) {
            logger::write("trace: fuzz: weapon 0x%X given to remote 0x%X", frame->args[0], static_cast<unsigned>(human));
        }
        return result;
    });
}

// FZ5, FZ6, FZ10: and taken from every remote (ecx = the human, edx = the weapon info, cdecl(flag)).
void __cdecl eachRemoteRemoveWeapon(GuestFrame* frame) {
    forEachRemote(frame, frame->regs.ecx, [&](uintptr_t human, bool expanded) {
        guest_call::Regs regs = frame->regs;
        regs.ecx = static_cast<uint32_t>(human);
        const uint32_t result = guest_call::call(sr2::kRemoveWeapon, regs, {frame->args[0]});
        if (expanded && tracing()) {
            logger::write("trace: fuzz: weapon 0x%X taken from remote 0x%X", frame->regs.edx, static_cast<unsigned>(human));
        }
        return result;
    });
}

// FZ9, FZ11: the pickup goes to every remote (ecx and al as the caller set them, cdecl(human, const weapon info*, flag)).
void __cdecl eachRemoteGivePickup(GuestFrame* frame) {
    forEachRemote(frame, frame->args[0], [&](uintptr_t human, bool expanded) {
        const uint32_t result = guest_call::call(sr2::kGivePickupWeapon, frame->regs, {static_cast<uint32_t>(human), frame->args[1], frame->args[2]});
        if (expanded && tracing()) {
            logger::write("trace: fuzz: pickup weapon 0x%X given to remote 0x%X", readDword(frame->args[1]), static_cast<unsigned>(human));
        }
        return result;
    });
}

// FZ8: the remote that stands in the crime area, the first in slot order, else the default partner (ebx is the crime).
uintptr_t __cdecl remoteInCrimeDistrict(uintptr_t crime) {
    const Remotes remotes;
    if (remotes.shipped()) return partner_scope::partnerHuman();
    const uint32_t crimeKey = readDword(crime + sr2::kFuzzCrimeDistrictKeyOffset);
    for (const uintptr_t human : remotes.humans().span()) {
        const uintptr_t record = readDword(human + sr2::kHumanDistrictOffset);
        const uint32_t key = record ? readDword(record + sr2::kDistrictKeyOffset) : 0;
        if (!fuzz_rules::inCrimeDistrict(record, key, crimeKey)) continue;
        if (human != remotes.defaultHuman() && diversion_trace::firstTime(g_areaSeen, human ^ (static_cast<uint64_t>(crimeKey) << 32))) {
            logger::write("trace: fuzz: remote 0x%X in the crime area 0x%X", static_cast<unsigned>(human), crimeKey);
        }
        return human;
    }
    return partner_scope::partnerHuman();
}

// FZ13: the cameraman films a fight when any remote is fighting (esi is the default partner).
uintptr_t __cdecl anyRemoteFighting(uintptr_t partner) {
    const Remotes remotes;
    if (remotes.shipped()) return guest_call::call(sr2::kHumanFighting, {.esi = static_cast<uint32_t>(partner)}) & 0xFF;
    for (const uintptr_t human : remotes.humans().span()) {
        if ((guest_call::call(sr2::kHumanFighting, {.esi = static_cast<uint32_t>(human)}) & 0xFF) == 0) continue;
        if (human != remotes.defaultHuman() && diversion_trace::dueNow(g_fightLimit)) {
            logger::write("trace: fuzz: cameraman: remote 0x%X fighting", static_cast<unsigned>(human));
        }
        return 1;
    }
    return 0;
}

// FZ14, FZ16: a crime NPC attacks a remote chosen at random. The game's own split between the local player and a remote stays.
uintptr_t __cdecl randomRemoteHuman() {
    const Remotes remotes;
    if (remotes.shipped()) return partner_scope::partnerHuman();
    g_rollState = fuzz_rules::nextRoll(g_rollState);
    const size_t index = fuzz_rules::pickIndex(g_rollState, remotes.humans().count);
    const uintptr_t human = remotes.humans().items[index];
    if (human != remotes.defaultHuman() && diversion_trace::dueNow(g_targetLimit)) {
        logger::write("trace: fuzz: target remote 0x%X (%zu of %zu)", static_cast<unsigned>(human), index + 1, remotes.humans().count);
    }
    return human;
}

// FZ17: a player is in the crime's vehicle when any remote is (ecx = the vehicle, eax = the default partner's handle).
uint32_t __cdecl anyRemoteInCrimeVehicle(uint32_t vehicle, uint32_t partnerHandle) {
    const Remotes remotes;
    if (remotes.shipped()) return guest_call::call(sr2::kVehicleHolds, {.ecx = vehicle, .eax = partnerHandle}) & 0xFF;
    for (const uintptr_t human : remotes.humans().span()) {
        const uint32_t handle = readDword(human + sr2::kObjectHandleOffset);
        if ((guest_call::call(sr2::kVehicleHolds, {.ecx = vehicle, .eax = handle}) & 0xFF) == 0) continue;
        if (human != remotes.defaultHuman() && diversion_trace::firstTime(g_vehicleSeen, human ^ (static_cast<uint64_t>(vehicle) << 32))) {
            logger::write("trace: fuzz: remote 0x%X in the crime vehicle 0x%X", static_cast<unsigned>(human), vehicle);
        }
        return 1;
    }
    return 0;
}

// FZ18: a spawn point counts as near the players when the remote nearest to the point is (`point` is the vec3 on the caller's stack).
uintptr_t __cdecl nearestRemoteToSpawnPoint(uint32_t point) {
    const Remotes remotes;
    if (remotes.shipped()) return partner_scope::partnerHuman();
    const uintptr_t nearest = partner_identity::nearest(point);
    if (!nearest) return partner_scope::partnerHuman();
    if (nearest != remotes.defaultHuman() && diversion_trace::dueNow(g_spawnLimit)) {
        logger::write("trace: fuzz: spawn point near remote 0x%X", static_cast<unsigned>(nearest));
    }
    return nearest;
}

// FZ15, FZ19: the whole predicate (thiscall(ecx, float radius), ret 4, AL) runs once per remote, with the getters answering that
// remote, until it holds.
uint32_t nearAnyRemote(GuestFrame* frame, void* original, rate_limit::RateLimit& limit, const char* what) {
    const uintptr_t predicate = reinterpret_cast<uintptr_t>(original);
    const Remotes remotes;
    if (remotes.shipped()) return guest_call::call(predicate, frame->regs, {frame->args[0]}) & 0xFF;
    const bool anyNear = partner_scope::anyRemoteHuman([&](uintptr_t human) {
        const bool holds = (guest_call::call(predicate, frame->regs, {frame->args[0]}) & 0xFF) != 0;
        if (holds && human != remotes.defaultHuman() && diversion_trace::dueNow(limit)) {
            logger::write("trace: fuzz: %s remote 0x%X", what, static_cast<unsigned>(human));
        }
        return holds;
    });
    return anyNear ? 1 : 0;
}

void __cdecl riotNearAnyRemote(GuestFrame* frame) { frame->regs.eax = nearAnyRemote(frame, g_riotNearOriginal, g_riotLimit, "riot near"); }
void __cdecl pointNearAnyRemote(GuestFrame* frame) { frame->regs.eax = nearAnyRemote(frame, g_pointNearOriginal, g_pointLimit, "point near"); }

// FZ20: the partner half of the test that a human joins the riot, for every remote. The human is esi, ebp is its position, the
// getter's answer is at [esp + 0x14] and the squared radius, a double, at [esp + 0x20]. AL is the answer.
void __cdecl rioterNearAnyRemote(BlockFrame* frame) {
    FpuGuard guard;
    const uintptr_t rioter = frame->regs.esi;
    const uint32_t rioterPosition = frame->regs.ebp;
    const uintptr_t given = *callerSlot(frame, sr2::kFuzzRioterPartnerSlot);
    double radiusSquared;
    std::memcpy(&radiusSquared, callerSlot(frame, sr2::kFuzzRioterRadiusSlot), sizeof(radiusSquared));
    const float heightWindow = readFloat(sr2::kFuzzRioterHeightWindow);
    const auto nearTo = [&](uintptr_t player) {
        const float distance = squared_distance::between(sr2::kSquaredDistanceTeam, static_cast<uint32_t>(player + sr2::kObjectPositionOffset), rioterPosition);
        const float heightGap = std::fabs(static_cast<float>(readFloat(rioter + sr2::kObjectPositionYOffset) - readFloat(player + sr2::kObjectPositionYOffset)));
        return fuzz_rules::rioterNear(distance, radiusSquared, heightGap, heightWindow);
    };

    const Remotes remotes;
    uintptr_t nearPlayer = 0;
    if (remotes.shipped()) {
        if (given && nearTo(given)) nearPlayer = given;
    } else {
        for (const uintptr_t human : remotes.humans().span()) {
            if (!nearTo(human)) continue;
            nearPlayer = human;
            break;
        }
    }
    frame->regs.eax = nearPlayer ? 1 : 0;
    if (nearPlayer && nearPlayer != remotes.defaultHuman() && diversion_trace::firstTime(g_rioterSeen, rioter)) {
        logger::write("trace: fuzz: rioter 0x%X joins near remote 0x%X", static_cast<unsigned>(rioter), static_cast<unsigned>(nearPlayer));
    }
}

PARTNER_WRAP_STUB(eachRemoteFuzzModeStub, eachRemoteFuzzMode, 8)
PARTNER_WRAP_STUB(eachRemoteGiveWeaponStub, eachRemoteGiveWeapon, 0)
PARTNER_WRAP_STUB(eachRemoteRemoveWeaponStub, eachRemoteRemoveWeapon, 0)
PARTNER_WRAP_STUB(eachRemoteGivePickupStub, eachRemoteGivePickup, 0)
PARTNER_REG_STUB(remoteInCrimeDistrictStub, ebx, remoteInCrimeDistrict)
PARTNER_REG_STUB(anyRemoteFightingStub, esi, anyRemoteFighting)
PARTNER_WRAP_STUB(riotNearStub, riotNearAnyRemote, 4)
PARTNER_WRAP_STUB(pointNearStub, pointNearAnyRemote, 4)
DIVERSION_BLOCK_STUB(rioterNearStub, rioterNearAnyRemote)

// Replaces `call 0x9B8CB0` (ecx = the vehicle, eax = a human handle; AL).
void __declspec(naked) anyRemoteInCrimeVehicleStub() {
    __asm {
        push eax
        push ecx
        call anyRemoteInCrimeVehicle
        add esp, 8
        ret
    }
}

// Replaces `call 0x9D7BD0` where the spawn point's vec3 is at [esp + 8] of the caller, [esp + 0xC] here.
void __declspec(naked) nearestRemoteToSpawnPointStub() {
    __asm {
        lea eax, [esp + 0xC]
        push eax
        call nearestRemoteToSpawnPoint
        add esp, 4
        ret
    }
}

}  // namespace

namespace fuzz_thunks {

void seed(uint32_t value) { g_rollState = value; }

partner_thunks::Binding bind(Thunk thunk) {
    switch (thunk) {
        case Thunk::EachRemoteFuzzMode: return {reinterpret_cast<void*>(&eachRemoteFuzzModeStub), nullptr};
        case Thunk::EachRemoteGiveWeapon: return {reinterpret_cast<void*>(&eachRemoteGiveWeaponStub), nullptr};
        case Thunk::EachRemoteRemoveWeapon: return {reinterpret_cast<void*>(&eachRemoteRemoveWeaponStub), nullptr};
        case Thunk::EachRemoteGivePickup: return {reinterpret_cast<void*>(&eachRemoteGivePickupStub), nullptr};
        case Thunk::RemoteInCrimeDistrictEbx: return {reinterpret_cast<void*>(&remoteInCrimeDistrictStub), nullptr};
        case Thunk::AnyRemoteFighting: return {reinterpret_cast<void*>(&anyRemoteFightingStub), nullptr};
        case Thunk::RandomRemoteHuman: return {reinterpret_cast<void*>(&randomRemoteHuman), nullptr};
        case Thunk::AnyRemoteInVehicleEcx: return {reinterpret_cast<void*>(&anyRemoteInCrimeVehicleStub), nullptr};
        case Thunk::NearestRemoteToStack8: return {reinterpret_cast<void*>(&nearestRemoteToSpawnPointStub), nullptr};
        case Thunk::FuzzRiotNearAnyRemote: return {reinterpret_cast<void*>(&riotNearStub), &g_riotNearOriginal};
        case Thunk::FuzzPointNearAnyRemote: return {reinterpret_cast<void*>(&pointNearStub), &g_pointNearOriginal};
        case Thunk::FuzzRioterNearAnyRemote: return {reinterpret_cast<void*>(&rioterNearStub), nullptr};
        default: return {nullptr, nullptr};
    }
}

}  // namespace fuzz_thunks
