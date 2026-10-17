#include "population.h"

#include <atomic>
#include <cstring>

#include "fpu_guard.h"
#include "guest_call.h"
#include "population_rules.h"
#include "remote_players.h"
#include "sr2_addresses.h"
#include "thunk_frame.h"

namespace {

using partner_sites::Thunk;
using population_rules::Point;

constexpr size_t kRadiusArg = 0;
constexpr size_t kBaseArg = 1;
constexpr size_t kWideArg = 0;

void* g_areaBudgetOriginal = nullptr;
void* g_clusterOriginal = nullptr;
std::atomic<uint32_t> g_lastAreasHundredths{0};

Point readPoint(uintptr_t address) {
    Point point{};
    memcpy(&point, reinterpret_cast<const void*>(address), sizeof(point));
    return point;
}

float readFloat(uintptr_t address) {
    float value = 0;
    memcpy(&value, reinterpret_cast<const void*>(address), sizeof(value));
    return value;
}

// The remote players of the session now, in ascending owner slot, with the role of this machine.
struct Remotes {
    uintptr_t humans[partner_rules::kMaxWalked];
    uint8_t slots[partner_rules::kMaxWalked];
    size_t count = 0;
    bool localIsHost = false;
    uint8_t localSlot = 0;
};

Remotes remotesNow() {
    remote_players::Snapshot snapshot;
    remote_players::take(snapshot);
    Remotes remotes;
    remotes.localIsHost = snapshot.localMember == snapshot.hostMember;
    for (size_t i = 0; i < snapshot.memberCount; ++i) {
        if (snapshot.members[i].local) remotes.localSlot = snapshot.members[i].slot;
    }
    for (size_t i = 0; i < snapshot.pairing.count; ++i) {
        if (!snapshot.pairing.items[i].human) continue;
        remotes.humans[remotes.count] = snapshot.pairing.items[i].human;
        remotes.slots[remotes.count++] = snapshot.pairing.items[i].slot;
    }
    return remotes;
}

// W1a: cdecl(float radius, uint base), the budget of one kind of ambient population.
void __cdecl areaBudget(GuestFrame* frame) {
    const uint32_t* args = frame->args;
    const Remotes remotes = remotesNow();
    if (remotes.count < population_rules::kBudgetRemotesAtLeast) {
        g_lastAreasHundredths = 0;
        frame->regs.eax = guest_call::call(reinterpret_cast<uintptr_t>(g_areaBudgetOriginal), frame->regs, {args[kRadiusArg], args[kBaseArg]});
        return;
    }
    FpuGuard guard;
    float radius = 0;
    memcpy(&radius, &args[kRadiusArg], sizeof(radius));
    Point cameras[partner_rules::kMaxWalked];
    for (size_t i = 0; i < remotes.count; ++i) cameras[i] = readPoint(remotes.humans[i] + sr2::kHumanCameraOffset);
    const float extra = population_rules::extraAreas(readPoint(sr2::kCameraPosition), cameras, remotes.count, radius);
    g_lastAreasHundredths = population_rules::areasHundredths(extra);
    frame->regs.eax = population_rules::budget(args[kBaseArg], extra);
}

// W2a: cdecl(char wide); the game's cluster test, 1 = far.
void __cdecl clusterLeaderFar(GuestFrame* frame) {
    const Remotes remotes = remotesNow();
    if (remotes.count == 0) {
        frame->regs.eax = guest_call::call(reinterpret_cast<uintptr_t>(g_clusterOriginal), frame->regs, {frame->args[kWideArg]});
        return;
    }
    FpuGuard guard;
    population_rules::Peer peers[partner_rules::kMaxWalked];
    for (size_t i = 0; i < remotes.count; ++i) {
        peers[i] = {remotes.slots[i], readPoint(remotes.humans[i] + sr2::kObjectPositionOffset)};
    }
    const bool wide = (frame->args[kWideArg] & 0xFF) != 0;
    const float radius = readFloat(wide ? sr2::kClusterRadiusWide : sr2::kClusterRadius);
    const Point local = readPoint(remote_players::localHuman() + sr2::kObjectPositionOffset);
    frame->regs.eax = population_rules::clusterFar(
remotes.localIsHost, remotes.localSlot, local, peers, remotes.count, radius) ? 1 : 0;
}

// W10b: esi = the vehicles still allowed (0x20 less this machine's own), returns it less 4 per remote player.
int32_t __cdecl vehicleHeadroom(int32_t free) {
    return population_rules::vehicleHeadroom(free, remote_players::remoteHumanCount());
}

PARTNER_WRAP_STUB(areaBudgetStub, areaBudget, 0)
PARTNER_WRAP_STUB(clusterLeaderFarStub, clusterLeaderFar, 0)

// Replaces the call to the partner getter, the test and the subtraction of 4 at the end of 0x93C1D0; the function's own
// `ret` follows after the pop of esi.
void __declspec(naked) vehicleHeadroomStub() {
    __asm {
        push esi
        call vehicleHeadroom
        add esp, 4
        pop esi
        ret
    }
}

}  // namespace

namespace population {

uint32_t lastAreasHundredths() { return g_lastAreasHundredths; }

partner_thunks::Binding bind(Thunk thunk) {
    switch (thunk) {
        case Thunk::AreaBudget: return {reinterpret_cast<void*>(&areaBudgetStub), &g_areaBudgetOriginal};
        case Thunk::ClusterLeaderFar: return {reinterpret_cast<void*>(&clusterLeaderFarStub), &g_clusterOriginal};
        case Thunk::VehicleHeadroom: return {reinterpret_cast<void*>(&vehicleHeadroomStub), nullptr};
        default:
 return {nullptr, nullptr};
    }
}

}  // namespace population
