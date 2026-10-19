// Blocks of the Crowd Control level start, end and protect start that the game runs for the local player and "the partner": the
// start weapon and flags, the end release and flags and the vehicle call of the protect phase now reach every remote.
#include "activity_state.h"
#include "crowd_control_remotes.h"
#include "crowd_control_rules.h"
#include "crowd_control_thunks.h"
#include "diversion_stubs.h"
#include "guest_call.h"
#include "log.h"
#include "memory.h"
#include "partner_scope.h"
#include "sr2_addresses.h"

namespace {

using partner_sites::Thunk;

// The arguments of the shipped weapon call 0xB71CF0(weapon, 1, -1, 0, 0).
constexpr uint32_t kGiveWeaponFlag = 1;
constexpr uint32_t kGiveWeaponNoSlot = 0xFFFFFFFF;
constexpr uint32_t kGiveWeaponOff = 0;
// The arguments of the shipped inventory call 0x55A930(inventory, value, 1, 0).
constexpr uint32_t kEquipFlag = 1;
constexpr uint32_t kEquipOff = 0;
constexpr uint32_t kFlagSet = 1;
constexpr uint32_t kFlagClear = 0;
constexpr uint32_t kReleaseAll = 1;
constexpr uint32_t kProtectFlag = 1;

const uintptr_t g_resumeProtectStart = sr2::kResumeProtectStart;
const uintptr_t g_resumeCrowdWeapon = sr2::kResumeCrowdWeapon;
const uintptr_t g_resumeCrowdStartFlags = sr2::kResumeCrowdStartFlags;
const uintptr_t g_resumeCrowdRelease = sr2::kResumeCrowdRelease;
const uintptr_t g_resumeCrowdEndFlags = sr2::kResumeCrowdEndFlags;

bool tracing() { return activity_state::tracing(); }

uint32_t asArgument(uintptr_t human) { return static_cast<uint32_t>(human); }

// Runs fn(human) once per remote player human, with the getters answering that remote. Nothing runs without a remote.
template <typename Fn>
void eachHuman(Fn fn) {
    partner_scope::eachRemoteHuman([&](uintptr_t human) {
        if (human) fn(human);
    });
}

// CR9: the vehicle call of the protect start (edi = human) runs for the local player, every remote and the celebrity that the
// game keeps at [esp + 0x10] and [esp + 0x18] when the loop starts.
void __cdecl protectStart(JumpFrame* frame) {
    const crowd_control_remotes::List remotes = crowd_control_remotes::take();
    uintptr_t order[crowd_control_rules::kMaxProtectOrder];
    const size_t count = crowd_control_rules::protectStartOrder(*gameSlot(frame, sr2::kProtectLocalSlot), remotes.humans, remotes.count,
                                                               *gameSlot(frame, sr2::kProtectCelebSlot), order);
    for (size_t i = 0; i < count; ++i) {
        if (!readDword(order[i] + sr2::kHumanVehicleOffset)) continue;
        guest_call::call(sr2::kProtectStartVehicle, {.edi = asArgument(order[i])}, {kProtectFlag});
        if (tracing()) logger::write("trace: crowd: protect start, vehicle call for human 0x%X", asArgument(order[i]));
    }
}

// CR10: every remote is given the bean bag gun of the level (ebx) at the start.
void __cdecl giveStartWeapon(JumpFrame* frame) {
    const uint32_t weapon = readDword(frame->regs.ebx + sr2::kCrowdWeaponOffset);
    eachHuman([&](uintptr_t human) {
        guest_call::call(sr2::kCrowdGiveWeapon, {.ecx = asArgument(human)}, {weapon, kGiveWeaponFlag, kGiveWeaponNoSlot, kGiveWeaponOff, kGiveWeaponOff});
        if (tracing()) logger::write("trace: crowd: start weapon 0x%X given to remote 0x%X", weapon, asArgument(human));
    });
}

// CR11: the start flags of every remote: ignore_equip_on_grab is set and the weapon in esi is equipped.
void __cdecl setStartFlags(JumpFrame* frame) {
    eachHuman([&](uintptr_t human) {
        guest_call::call(sr2::kSyncIgnoreEquipOnGrab, {}, {asArgument(human), kFlagSet});
        guest_call::call(sr2::kInventoryEquip, {}, {asArgument(human + sr2::kHumanInventoryOffset), frame->regs.esi, kEquipFlag, kEquipOff});
        if (tracing()) logger::write("trace: crowd: start flags set for remote 0x%X", asArgument(human));
    });
}

// CR14: every remote lets go of what it holds at the end (the local half of the release stays the game's).
void __cdecl releaseHeld(JumpFrame*) {
    eachHuman([](uintptr_t human) {
        const uint32_t held = readDword(human + sr2::kHumanHeldOffset);
        if (!held) return;
        guest_call::call(sr2::kReleaseHeldAtEnd, {.ecx = asArgument(human)}, {kReleaseAll});
        if (tracing()) logger::write("trace: crowd: end, remote 0x%X lets go of 0x%X", asArgument(human), held);
    });
}

// CR15: the end flags of every remote: ignore_equip_on_grab and beer_muscles are cleared.
void __cdecl clearEndFlags(JumpFrame*) {
    eachHuman([](uintptr_t human) {
        guest_call::call(sr2::kSyncIgnoreEquipOnGrab, {}, {asArgument(human), kFlagClear});
        guest_call::call(sr2::kForceBeerMuscles, {}, {asArgument(human), kFlagClear});
        if (tracing()) logger::write("trace: crowd: end flags cleared for remote 0x%X", asArgument(human));
    });
}

DIVERSION_JUMP_STUB(protectStartStub, protectStart, g_resumeProtectStart)
DIVERSION_JUMP_STUB(giveStartWeaponStub, giveStartWeapon, g_resumeCrowdWeapon)
DIVERSION_JUMP_STUB(setStartFlagsStub, setStartFlags, g_resumeCrowdStartFlags)
DIVERSION_JUMP_STUB(releaseHeldStub, releaseHeld, g_resumeCrowdRelease)
DIVERSION_JUMP_STUB(clearEndFlagsStub, clearEndFlags, g_resumeCrowdEndFlags)

}  // namespace

namespace crowd_control_thunks {

partner_thunks::Binding bindEach(Thunk thunk) {
    switch (thunk) {
        case Thunk::EachPlayerProtectStart: return {reinterpret_cast<void*>(&protectStartStub), nullptr};
        case Thunk::EachRemoteCrowdWeapon: return {reinterpret_cast<void*>(&giveStartWeaponStub), nullptr};
        case Thunk::EachRemoteCrowdStartFlags: return {reinterpret_cast<void*>(&setStartFlagsStub), nullptr};
        case Thunk::EachRemoteReleaseHeld: return {reinterpret_cast<void*>(&releaseHeldStub), nullptr};
        case Thunk::EachRemoteCrowdEndFlags: return {reinterpret_cast<void*>(&clearEndFlagsStub), nullptr};
        default: return {nullptr, nullptr};
    }
}

}  // namespace crowd_control_thunks
