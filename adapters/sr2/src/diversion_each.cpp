// Calls and loops of the diversions that run once per remote: the game's code does for the local player and "the partner"
// what every remote now gets: activity weapons, followers, setup, vehicle preparation, credit and the Snatch delivery. Phase B
// (partner_experimental) adds the Mayhem district test for every player and the extra Trafficking seats.
#include <bit>

#include "activity_state.h"
#include "diversion_remotes.h"
#include "diversion_rules.h"
#include "diversion_stubs.h"
#include "diversion_thunks.h"
#include "fpu_guard.h"
#include "guest_call.h"
#include "log.h"
#include "memory.h"
#include "partner_scope.h"
#include "remote_players.h"
#include "sr2_addresses.h"
#include "thunk_frame.h"

namespace {

using partner_sites::Thunk;

constexpr uint32_t kNoVehicleClass = 0xFFFFFFFF;  // ECX of the vehicle preparation: -1
constexpr uint32_t kPrepareFlagA = 1;
constexpr uint32_t kPrepareFlagB = 1;
constexpr size_t kMayhemCreditAmountSlot = 0x0C;  // the float argument of 0x6685E0, at [esp + 0xC] when the block starts
constexpr uint32_t kDistrictNone = 0xFFFFFFFF;
constexpr uint32_t kSetupLookFlag = 1;
constexpr uint32_t kSetupWeaponsFlag = 0;
constexpr uint32_t kSeatFlagBytes = 11;
constexpr size_t kMaxExtraSeats = 2;

const uintptr_t g_resumeSnatchSetup = sr2::kResumeSnatchSetup;
const uintptr_t g_resumeSnatchVehicles = sr2::kResumeSnatchVehicles;
const uintptr_t g_resumeSnatchDelivery = sr2::kResumeSnatchDelivery;
const uintptr_t g_resumeTraffickingVehicles = sr2::kResumeTraffickingVehicles;
const uintptr_t g_resumeMayhemCredit = sr2::kResumeMayhemCredit;
const uintptr_t g_resumeMayhemDistrict = sr2::kResumeMayhemDistrict;

bool tracing() { return activity_state::tracing(); }

// Runs fn(human) once per remote player human, with the getters answering that remote. Nothing runs without a remote: the
// callees test the human first, so a null human did nothing in the shipped game either.
template <typename Fn>
void eachHuman(Fn fn) {
    partner_scope::eachRemoteHuman([&](uintptr_t human) {
        if (human) fn(human);
    });
}

// DVK2: the followers of every remote are dismissed (cdecl(0), edi = human, the caller pops).
void __cdecl dismissFollowers(GuestFrame* frame) {
    eachHuman([&](uintptr_t human) {
        guest_call::call(sr2::kDismissFollowers, {.edi = static_cast<uint32_t>(human)}, {frame->args[0]});
        if (tracing()) logger::write("trace: trafficking: followers of 0x%X dismissed", static_cast<unsigned>(human));
    });
}

// DVK3, DVM1: the activity weapons are given to every remote (eax = level, edi = human).
void armEach(GuestFrame* frame, uintptr_t function, const char* what) {
    eachHuman([&](uintptr_t human) {
        guest_call::call(function, {.edi = static_cast<uint32_t>(human), .eax = frame->regs.eax});
        if (tracing()) logger::write("trace: %s weapons for remote 0x%X given", what, static_cast<unsigned>(human));
    });
}

void __cdecl traffickingArm(GuestFrame* frame) { armEach(frame, sr2::kTraffickingArm, "trafficking:"); }
void __cdecl mayhemArm(GuestFrame* frame) { armEach(frame, sr2::kMayhemArm, "mayhem:"); }

// DVK4, DVM2: and taken from every remote (eax = level, stdcall(human)).
void disarmEach(GuestFrame* frame, uintptr_t function, const char* what) {
    eachHuman([&](uintptr_t human) {
        guest_call::call(function, {.eax = frame->regs.eax}, {static_cast<uint32_t>(human)});
        if (tracing()) logger::write("trace: %s weapons for remote 0x%X removed", what, static_cast<unsigned>(human));
    });
}

void __cdecl traffickingDisarm(GuestFrame* frame) { disarmEach(frame, sr2::kTraffickingDisarm, "trafficking:"); }
void __cdecl mayhemDisarm(GuestFrame* frame) { disarmEach(frame, sr2::kMayhemDisarm, "mayhem:"); }

// DVM5: the Mayhem complication is started for every remote (cdecl(human, flag), the caller pops).
void __cdecl complicationEach(GuestFrame* frame) {
    eachHuman([&](uintptr_t human) {
        guest_call::call(sr2::kMayhemComplication, {}, {static_cast<uint32_t>(human), frame->args[1]});
    });
}

// DVM3: the district credit of Mayhem goes to every remote. The float of the call is the caller's argument at [esp + 0xC].
void __cdecl mayhemCredit(JumpFrame* frame) {
    const uint32_t amount = *gameSlot(frame, kMayhemCreditAmountSlot);
    eachHuman([&](uintptr_t human) {
        guest_call::call(sr2::kMayhemCredit, {.esi = readDword(human + sr2::kObjectHandleOffset)},
                         {0, amount, static_cast<uint32_t>(human), 0});
        if (tracing()) logger::write("trace: mayhem: credit for remote 0x%X", static_cast<unsigned>(human));
    });
}

// DVP3: the per-player setup of a Snatch start for every remote.
void __cdecl snatchSetup(JumpFrame*) {
    eachHuman([](uintptr_t human) {
        guest_call::call(sr2::kSnatchSetupBoth, {.ecx = static_cast<uint32_t>(human)}, {sr2::kSnatchSetupBothArg});
        guest_call::call(sr2::kSnatchSetupLook, {}, {static_cast<uint32_t>(human), kSetupLookFlag});
        guest_call::call(sr2::kSnatchSetupWeapons, {}, {static_cast<uint32_t>(human), kSetupWeaponsFlag});
        if (tracing()) logger::write("trace: snatch: setup for remote 0x%X", static_cast<unsigned>(human));
    });
}

// The vehicle preparation of a start: ecx = -1, edx = the vehicle, cdecl(1.0f, 1, 1).
void prepareVehicle(uintptr_t vehicle) {
    guest_call::call(sr2::kVehiclePrepare, {.edx = static_cast<uint32_t>(vehicle), .ecx = kNoVehicleClass},
                     {sr2::kFloatOne, kPrepareFlagA, kPrepareFlagB});
}

// DVP4b: every remote's vehicle is prepared.
void __cdecl snatchVehicles(JumpFrame*) {
    eachHuman([](uintptr_t human) {
        const uintptr_t vehicle = diversion_remotes::vehicleOf(human);
        if (vehicle) prepareVehicle(vehicle);
    });
}

// DVK6: the vehicle of every remote that is not the local player's is stopped (esi points at the local vehicle handle).
void __cdecl stopVehicles(JumpFrame* frame) {
    const uint32_t localVehicle = readDword(frame->regs.esi);
    eachHuman([&](uintptr_t human) {
        const uint32_t handle = readDword(human + sr2::kHumanVehicleOffset);
        if (!handle || handle == localVehicle) return;
        const uintptr_t vehicle = guest_call::call(sr2::kVehicleObjectOf, {.ecx = handle});
        prepareVehicle(vehicle);
        if (tracing()) logger::write("trace: trafficking: vehicle of remote 0x%X stopped", static_cast<unsigned>(human));
    });
}

// DVP8: the Snatch delivery test runs for the local player against the first marker and for every remote against the second.
// A player delivers when its distance to the marker is within 2.0 in a vehicle and 3.5 on foot, and the marker accepts.
float floatAt(uintptr_t address) { return std::bit_cast<float>(readDword(address)); }

void deliverIfInRange(uintptr_t level, uintptr_t player, size_t rank) {
    const uintptr_t marker = readDword(level + sr2::kSnatchMarkerOffset + sizeof(uint32_t) * diversion_rules::deliveryMarker(rank));
    if (!marker || (readByte(marker + sr2::kSnatchMarkerFlagsOffset) & sr2::kSnatchMarkerClosedBit) != 0) return;
    const bool inVehicle = readDword(player + sr2::kHumanVehicleOffset) != 0;
    const float radius = floatAt(inVehicle ? sr2::kSnatchRadiusInVehicle : sr2::kSnatchRadiusOnFoot);
    if (!diversion_rules::inDeliveryRange(remote_players::positionOf(player), remote_players::positionOf(marker), radius)) return;
    guest_call::call(sr2::kSnatchDeliver, {}, {static_cast<uint32_t>(level), static_cast<uint32_t>(player)});
}

void __cdecl snatchDelivery(JumpFrame* frame) {
    FpuGuard guard;
    const uintptr_t level = frame->regs.ebp;
    size_t rank = 0;
    const uintptr_t local = remote_players::localHuman();
    if (local) deliverIfInRange(level, local, rank);
    diversion_remotes::View view;
    diversion_remotes::take(view);
    for (size_t i = 0; i < view.count(); ++i) {
        if (view.items()[i].human) deliverIfInRange(level, view.items()[i].human, ++rank);
    }
}

// Phase B, DVM6: "every player is in the Mayhem district" for the local player and every remote, not the pair {local, partner}.
// The other player of a test is the default partner for the local player and the local player for a remote.
uintptr_t districtRecordOf(uintptr_t player, uintptr_t other) {
    const uintptr_t record = readDword(player + sr2::kHumanDistrictOffset);
    if (record) return record;
    const uint32_t hint = other ? readDword(other + sr2::kHumanDistrictOffset) : 0;
    const uint32_t index = guest_call::call(sr2::kDistrictOfPoint, {}, {static_cast<uint32_t>(player + sr2::kObjectPositionOffset), hint});
    if (index == kDistrictNone) return 0;
    return index * sr2::kDistrictRecordStride + readDword(sr2::kDistrictRecords);
}

void checkMayhemPlayer(uintptr_t level, uintptr_t player, uintptr_t other) {
    uint8_t& allIn = byteField(level, sr2::kMayhemAllInOffset);
    const uintptr_t record = districtRecordOf(player, other);
    if (!record) {
        allIn = 0;
        return;
    }
    const uintptr_t targets = readDword(level + sr2::kLevelInstanceOffset);
    const uintptr_t target = targets + readDword(level + sr2::kMayhemTargetIndexOffset) * sr2::kMayhemTargetStride;
    const bool in = readDword(record + sr2::kDistrictKeyOffset) == readDword(target + sr2::kMayhemTargetKeyOffset);
    allIn = static_cast<uint8_t>(allIn & (in ? 1 : 0));
    if (!readByte(level + sr2::kMayhemTimerOnOffset)) return;
    if (in) {
        guest_call::call(sr2::kMayhemPlayerIn, {.ecx = static_cast<uint32_t>(player)}, {static_cast<uint32_t>(level)});
    } else {
        guest_call::call(sr2::kMayhemPlayerOut, {}, {static_cast<uint32_t>(level), static_cast<uint32_t>(player), 1});
    }
}

void __cdecl mayhemDistrictAllPlayers(JumpFrame* frame) {
    const uintptr_t level = frame->regs.ebx;
    const uintptr_t local = remote_players::localHuman();
    const uintptr_t partner = partner_scope::partnerHuman();
    if (local) checkMayhemPlayer(level, local, partner);
    diversion_remotes::View view;
    diversion_remotes::take(view);
    for (size_t i = 0; i < view.count(); ++i) {
        if (view.items()[i].human) checkMayhemPlayer(level, view.items()[i].human, local);
    }
    if (tracing()) logger::write("trace: mayhem: district all players in = %u", readByte(level + sr2::kMayhemAllInOffset));
}

// Phase B, DVK7: the Trafficking seat loop seats {dealer, local, partner}; the other remotes sit in the next seats with the
// request the loop builds (its flag bytes are what the loop stores; ebx is the zero it keeps, ebp the mode, edi the vehicle).
#pragma pack(push, 1)
struct SeatRequest {
    uint32_t occupant;
    uint32_t vehicle;
    uint32_t mode;
    uint32_t seat;
    uint8_t flags[kSeatFlagBytes];
};
#pragma pack(pop)
static_assert(sizeof(SeatRequest) == sr2::kSeatRequestBytes, "the seat request is the 0x1B bytes the loop builds");

void __cdecl traffickingExtraSeats(JumpFrame* frame) {
    const uint8_t zero = static_cast<uint8_t>(frame->regs.ebx);
    diversion_remotes::View view;
    diversion_remotes::take(view);
    size_t seated = 0;
    for (size_t i = 0; i < view.count() && seated < kMaxExtraSeats; ++i) {
        if (!view.items()[i].human || i == view.defaultIndex) continue;
        SeatRequest request = {static_cast<uint32_t>(view.items()[i].human), frame->regs.edi, frame->regs.ebp,
                               sr2::kFirstExtraSeat + static_cast<uint32_t>(seated),
                               {1, zero, zero, 1, zero, zero, 1, 1, zero, zero, zero}};
        const uint32_t result = guest_call::call(sr2::kSeatPlace, {.eax = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&request))});
        if (tracing()) {
            logger::write("trace: trafficking: remote rank %u seat %u -> 0x%X", static_cast<unsigned>(i), request.seat, result);
        }
        ++seated;
    }
}

PARTNER_WRAP_STUB(dismissFollowersStub, dismissFollowers, 0)
PARTNER_WRAP_STUB(traffickingArmStub, traffickingArm, 0)
PARTNER_WRAP_STUB(traffickingDisarmStub, traffickingDisarm, 4)
PARTNER_WRAP_STUB(mayhemArmStub, mayhemArm, 0)
PARTNER_WRAP_STUB(mayhemDisarmStub, mayhemDisarm, 4)
PARTNER_WRAP_STUB(complicationEachStub, complicationEach, 0)
DIVERSION_JUMP_STUB(mayhemCreditStub, mayhemCredit, g_resumeMayhemCredit)
DIVERSION_JUMP_STUB(snatchSetupStub, snatchSetup, g_resumeSnatchSetup)
DIVERSION_JUMP_STUB(snatchVehiclesStub, snatchVehicles, g_resumeSnatchVehicles)
DIVERSION_JUMP_STUB(stopVehiclesStub, stopVehicles, g_resumeTraffickingVehicles)
DIVERSION_JUMP_STUB(snatchDeliveryStub, snatchDelivery, g_resumeSnatchDelivery)
DIVERSION_JUMP_STUB(mayhemDistrictStub, mayhemDistrictAllPlayers, g_resumeMayhemDistrict)

// The block this replaces is the epilogue of the seat loop's function, so the stub ends with it.
void __declspec(naked) traffickingExtraSeatsStub() {
    __asm {
        pushfd
        pushad
        push esp
        call traffickingExtraSeats
        add esp, 4
        popad
        popfd
        pop edi
        pop esi
        pop ebp
        pop ebx
        add esp, 0x34
        ret
    }
}

}  // namespace

namespace diversion_thunks {

partner_thunks::Binding bindEach(Thunk thunk) {
    switch (thunk) {
        case Thunk::EachRemoteDismissFollowers: return {reinterpret_cast<void*>(&dismissFollowersStub), nullptr};
        case Thunk::EachRemoteTraffickingArm: return {reinterpret_cast<void*>(&traffickingArmStub), nullptr};
        case Thunk::EachRemoteTraffickingDisarm: return {reinterpret_cast<void*>(&traffickingDisarmStub), nullptr};
        case Thunk::EachRemoteMayhemArm: return {reinterpret_cast<void*>(&mayhemArmStub), nullptr};
        case Thunk::EachRemoteMayhemDisarm: return {reinterpret_cast<void*>(&mayhemDisarmStub), nullptr};
        case Thunk::EachRemote9C8C20: return {reinterpret_cast<void*>(&complicationEachStub), nullptr};
        case Thunk::EachRemoteMayhemCredit: return {reinterpret_cast<void*>(&mayhemCreditStub), nullptr};
        case Thunk::EachRemoteSnatchSetup: return {reinterpret_cast<void*>(&snatchSetupStub), nullptr};
        case Thunk::EachRemoteVehicleAAAB60: return {reinterpret_cast<void*>(&snatchVehiclesStub), nullptr};
        case Thunk::EachRemoteStopVehicle: return {reinterpret_cast<void*>(&stopVehiclesStub), nullptr};
        case Thunk::SnatchDeliveryEachPlayer: return {reinterpret_cast<void*>(&snatchDeliveryStub), nullptr};
        case Thunk::MayhemDistrictAllPlayers: return {reinterpret_cast<void*>(&mayhemDistrictStub), nullptr};
        case Thunk::TraffickingExtraSeats: return {reinterpret_cast<void*>(&traffickingExtraSeatsStub), nullptr};
        default: return {nullptr, nullptr};
    }
}

}  // namespace diversion_thunks
