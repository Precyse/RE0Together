#include "population.h"

#include <atomic>
#include <cstring>
#include <mutex>

#include "fpu_guard.h"
#include "guest_call.h"
#include "log.h"
#include "memory.h"
#include "population_rules.h"
#include "remote_players.h"
#include "sr2_addresses.h"
#include "thunk_frame.h"
#include "traffic_list.h"
#include "traffic_trace_rules.h"
#include "vehicle_pool.h"

namespace {

using partner_sites::Thunk;
using population_rules::Point;

constexpr size_t kRadiusArg = 0;
constexpr size_t kBaseArg = 1;
constexpr size_t kWideArg = 0;

void* g_areaBudgetOriginal = nullptr;
void* g_clusterOriginal = nullptr;
std::atomic<uint32_t> g_lastAreasHundredths{0};
bool g_trace = false;
std::mutex g_quotaMutex;
population::LastQuota g_lastQuota;  // the quota of the last headroom evaluation, for the census and the change test

Point readPoint(uintptr_t address) {
    Point point{};
    memcpy(&point, reinterpret_cast<const void*>(address), sizeof(point));
    return point;
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
    // The traffic vehicle cap (base 12, 0x93D2B0): with the 64 vehicle pool it grows with the areas, up to the class 1 limit the
    // quotas share out.
    const bool trafficCap = frame->returnAddress == sr2::kTrafficCapBudgetReturn;
    frame->regs.eax = trafficCap && vehicle_pool::installed() ? population_rules::budgetUncapped(args[kBaseArg], extra)
                                                                  : population_rules::budget(args[kBaseArg], extra);
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

// The traffic quota of this machine now (population_rules::trafficQuota over the players with a human) and the cap it shares.
population_rules::Quota quotaNow(int32_t& limit) {
    population_rules::Player players[population_rules::kMaxTrafficPlayers];
    uint8_t leaders[population_rules::kMaxTrafficPlayers];
    uint8_t localSlot = 0;
    const size_t count = traffic_list::players(players, population_rules::kMaxTrafficPlayers, localSlot);
    limit = static_cast<int32_t>(readDword(sr2::kCategoryClass1Limit));
    population_rules::trafficLeaders(players, count, readFloat(sr2::kClusterRadius), leaders);
    return population_rules::trafficQuota(limit, players, leaders, count, localSlot);
}

// trace=1: the quota line when the quota or the set of players this machine leads changed.
void traceQuota(const population_rules::Quota& quota, int32_t limit, size_t owned) {
    if (!g_trace) return;
    std::lock_guard<std::mutex> lock(g_quotaMutex);
    const bool changed = !traffic_trace_rules::sameQuota(g_lastQuota.quota, g_lastQuota.limit, quota, limit);
    g_lastQuota = {quota, limit};
    if (changed) logger::write("%s", traffic_trace_rules::quotaLine(quota, limit, owned).c_str());
}

// W10b: the vehicles still allowed, in place of `0x20 - own vehicles` less 4 once: the pool rule (4 off per remote session
// member), and with 2 or more remote members also what is left of this machine's traffic quota.
int32_t __cdecl vehicleHeadroom() {
    const size_t members = remote_players::remoteMemberCount();
    const int32_t capacity = static_cast<int32_t>(vehicle_pool::capacity());
    const int32_t inUse = static_cast<int32_t>(readDword(sr2::kVehicleCount));
    if (members <= 1) return population_rules::vehicleHeadroom(capacity, inUse, members, 0, 0);
    FpuGuard guard;
    int32_t limit = 0;
    const population_rules::Quota quota = quotaNow(limit);
    const size_t owned = traffic_list::ownedCount();
    traceQuota(quota, limit, owned);
    return population_rules::vehicleHeadroom(capacity, inUse, members, quota.quota, static_cast<int32_t>(owned));
}

PARTNER_WRAP_STUB(areaBudgetStub, areaBudget, 0)
PARTNER_WRAP_STUB(clusterLeaderFarStub, clusterLeaderFar, 0)

// Replaces the call to the partner getter, the test and the subtraction of 4 at the end of 0x93C1D0; the function's own
// `ret` follows after the pop of esi.
void __declspec(naked) vehicleHeadroomStub() {
    __asm {
        call vehicleHeadroom
        pop esi
        ret
    }
}

}  // namespace

namespace population {

uint32_t lastAreasHundredths() { return g_lastAreasHundredths; }

LastQuota lastQuota() {
    std::lock_guard<std::mutex> lock(g_quotaMutex);
    return g_lastQuota;
}

void setTrace(bool trace) { g_trace = trace; }

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
