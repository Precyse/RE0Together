#include "vehicle_seating.h"

#include <span>
#include <string>

#include "guest_call.h"
#include "log.h"
#include "memory.h"
#include "partner_scope.h"
#include "remote_players.h"
#include "site_installer.h"
#include "sr2_addresses.h"
#include "thunk_frame.h"
#include "vehicle_seating_rules.h"
#include "vehicle_seating_sites.h"
#include "vehicle_seating_state.h"

namespace {

using partner_sites::Site;
using partner_sites::Thunk;
using vehicle_seating_rules::Occupant;

constexpr size_t kSeatArg = 4;       // SE1: the enter request block (dword 0 = seat) sits 0x10 above the stack at the replaced call

struct Set {
    const char* name;
    std::span<const Site> sites;
};

constexpr Set kSets[] = {
    {"trafficking", vehicle_seating_sites::kTrafficking},
    {"checkpoint", vehicle_seating_sites::kCheckpoint},
    {"mission exit", vehicle_seating_sites::kMissionExit},
};

bool g_trace = false;
uint32_t g_missionExitPartnerDone = sr2::kMissionExitPartnerDone;

// What SE3 did, for the line SE4 logs.
size_t g_stoppedCars = 0;
size_t g_remoteCars = 0;

// The player humans of the remotes, in ascending slot order.
struct Remotes {
    partner_rules::Remote items[partner_rules::kMaxWalked];
    size_t count = 0;
};

Remotes remotesWithHuman() {
    remote_players::Snapshot snapshot;
    remote_players::take(snapshot);
    Remotes remotes;
    for (size_t i = 0; i < snapshot.pairing.count; ++i) {
        if (snapshot.pairing.items[i].human) remotes.items[remotes.count++] = snapshot.pairing.items[i];
    }
    return remotes;
}

Occupant occupantOf(uintptr_t vehicle, uint32_t seat) {
    const uint32_t handle = readDword(vehicle + sr2::kVehicleSeatsOffset + seat * sr2::kVehicleSeatStride);
    const uintptr_t object = remote_players::objectOfHandle(handle);
    if (!object) return Occupant::Empty;
    return object == remote_players::localHuman() || remote_players::isRemotePlayerHuman(object) ? Occupant::Player : Occupant::Npc;
}

uint32_t seatCountOf(uintptr_t vehicle) {
    return vehicle_seating_rules::seatCount(readByte(vehicle + sr2::kVehicleSeatsDeclaredOffset),
                                            readDword(vehicle + sr2::kVehicleSeatCountOffset));
}

// SE1: replaces `call 0x9D7BD0` of the "is this the partner's car" test in the enter button handler; esi = the vehicle found, edi = the
// entering human, args[4] = the seat of the request (2). al answers whether it is a remote's car. The seat becomes a free one.
void __cdecl traffickingRemoteSeat(GuestFrame* frame) {
    const uintptr_t vehicle = frame->regs.esi;
    const uint32_t handle = readDword(vehicle + sr2::kObjectHandleOffset);
    const Remotes remotes = remotesWithHuman();
    uint32_t cars[partner_rules::kMaxWalked] = {};
    for (size_t i = 0; i < remotes.count; ++i) cars[i] = readDword(remotes.items[i].human + sr2::kHumanVehicleOffset);
    const std::span<const uint32_t> remoteCars(cars, remotes.count);
    if (!vehicle_seating_rules::isRemoteVehicle(handle, remoteCars)) {
        frame->regs.eax = 0;
        if (g_trace) logger::write("seating: trafficking car %08X is no remote's car", handle);
        return;
    }
    frame->regs.eax = 1;
    Occupant seats[vehicle_seating_rules::kMaxSeats] = {};
    const uint32_t count = seatCountOf(vehicle);
    for (uint32_t seat = 0; seat < count; ++seat) seats[seat] = occupantOf(vehicle, seat);
    const auto scripted = static_cast<int32_t>(frame->args[kSeatArg]);
    const int32_t chosen = vehicle_seating_rules::chooseTraffickingSeat(std::span<const Occupant>(seats, count), scripted);
    frame->args[kSeatArg] = static_cast<uint32_t>(chosen);
    if (!g_trace) return;
    size_t owner = 0;
    while (cars[owner] != handle) ++owner;
    const bool full = scripted >= 0 && static_cast<uint32_t>(scripted) < count && chosen == scripted && seats[scripted] == Occupant::Player;
    logger::write("seating: trafficking car %08X of slot %u, %u seats, seat %d -> %d%s", handle, static_cast<unsigned>(remotes.items[owner].slot),
                  count, scripted, chosen, full ? " (full)" : "");
}

// SE2: replaces `call 0x9D7BD0` of the checkpoint restore (ebp = the checkpoint record, not needed): the human whose car the player
// is put into. The remote MC4 recorded while it is still paired, else the default partner, which is never 0 where the game's is not.
uintptr_t __cdecl checkpointVehicleHolder(uint32_t) {
    const Remotes remotes = remotesWithHuman();
    uint8_t pairedSlots[partner_rules::kMaxWalked];
    for (size_t i = 0; i < remotes.count; ++i) pairedSlots[i] = remotes.items[i].slot;
    const int32_t remembered = vehicle_seating_state::checkpointHolder();
    const int32_t slot = vehicle_seating_rules::restoreHolderSlot(remembered, std::span<const uint8_t>(pairedSlots, remotes.count));
    uintptr_t human = 0;
    for (size_t i = 0; i < remotes.count && slot != vehicle_seating_rules::kDefaultHolder; ++i) {
        if (remotes.items[i].slot == slot) human = remotes.items[i].human;
    }
    if (g_trace) logger::writeUnlessRepeated("seating: checkpoint restore into the car of slot %d (remembered %d)", slot, remembered);
    return human ? human : partner_scope::partnerHuman();
}

// Stops the car of `human` the way the mission exit stops the player's: when the human is settled in a vehicle that has a component,
// the component's vtable +0x20 is called with 0. True when the call was made.
bool stopCarOf(uintptr_t human) {
    const uintptr_t vehicle = guest_call::call(sr2::kVehicleOfHandleChecked, {.ecx = readDword(human + sr2::kHumanVehicleOffset)});
    if (!vehicle) return false;
    const bool settled = static_cast<uint8_t>(guest_call::call(sr2::kHumanInVehicle, {.edi = static_cast<uint32_t>(vehicle), .esi = static_cast<uint32_t>(human)}));
    const uintptr_t component = readDword(vehicle + sr2::kVehicleComponentOffset);
    if (!settled || !component) return false;
    const uintptr_t stop = readDword(readDword(component) + sr2::kComponentStopSlot);
    guest_call::call(stop, {.ecx = static_cast<uint32_t>(component)}, {0});
    return true;
}

// SE3: replaces the default partner's stop in the mission exit (`call 0x9D7BD0; mov esi, eax; test esi, esi; je`). Every remote's car
// is stopped; the default partner goes on in esi for the release that follows.
uintptr_t __cdecl missionExitStopRemotes() {
    const Remotes remotes = remotesWithHuman();
    g_remoteCars = remotes.count;
    g_stoppedCars = 0;
    for (size_t i = 0; i < remotes.count; ++i) {
        if (stopCarOf(remotes.items[i].human)) ++g_stoppedCars;
    }
    return partner_scope::partnerHuman();
}

// SE4: replaces the default partner's `call 0x69DDA0` (edx = the partner) with the release of every remote's hold.
void __cdecl missionExitReleaseRemotes() {
    const Remotes remotes = remotesWithHuman();
    for (size_t i = 0; i < remotes.count; ++i) guest_call::call(sr2::kReleaseHeld, {.edx = static_cast<uint32_t>(remotes.items[i].human)});
    if (g_trace) {
        logger::write("seating: mission exit stopped %zu of %zu remote cars, released %zu remote holds", g_stoppedCars, g_remoteCars, remotes.count);
    }
}

PARTNER_WRAP_STUB(traffickingStub, traffickingRemoteSeat, 0)
PARTNER_REG_STUB(checkpointStub, ebp, checkpointVehicleHolder)
PARTNER_BLOCK_STUB(missionExitReleaseStub, missionExitReleaseRemotes)

// Entered by the jump of SE3 in place of the replaced block: the default partner comes back in esi, then the mission exit goes on
// with the local player's release.
void __declspec(naked) missionExitStopStub() {
    __asm pushad
    __asm call missionExitStopRemotes
    __asm mov [esp + 4], eax
    __asm popad
    __asm jmp dword ptr [g_missionExitPartnerDone]
}

std::string countText(size_t count) { return std::to_string(count) + (count == 1 ? " site" : " sites"); }

}  // namespace

namespace vehicle_seating {

partner_thunks::Binding bind(Thunk thunk) {
    switch (thunk) {
        case Thunk::TraffickingRemoteSeat: return {reinterpret_cast<void*>(&traffickingStub), nullptr};
        case Thunk::CheckpointVehicleHolder: return {reinterpret_cast<void*>(&checkpointStub), nullptr};
        case Thunk::MissionExitStopRemotes: return {reinterpret_cast<void*>(&missionExitStopStub), nullptr};
        case Thunk::MissionExitReleaseRemotes: return {reinterpret_cast<void*>(&missionExitReleaseStub), nullptr};
        default: return {nullptr, nullptr};
    }
}

void install(bool trace) {
    g_trace = trace;
    std::string patched;
    const auto installSet = [&](const char* name, std::span<const Site> sites) {
        if (!site_installer::sitesMatch(sites)) {
            logger::write("seating: %s not patched, the bytes differ", name);
            return;
        }
        const site_installer::Result result = site_installer::install(sites, &bind);
        if (!result.installed) {
            logger::write("seating: %s not patched, a thunk is missing", name);
            return;
        }
        if (!patched.empty()) patched += ", ";
        patched += std::string(name) + " " + countText(result.patched);
    };
    for (const Set& set : kSets) installSet(set.name, set.sites);
    if (!patched.empty()) logger::write("seating: %s patched", patched.c_str());
}

}  // namespace vehicle_seating
