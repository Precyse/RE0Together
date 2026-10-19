#include "traffic_trace.h"

#include <windows.h>

#include <algorithm>
#include <mutex>

#include "fpu_guard.h"
#include "log.h"
#include "memory.h"
#include "population.h"
#include "rate_limit.h"
#include "remote_players.h"
#include "sr2_addresses.h"
#include "thunk_frame.h"
#include "traffic_list.h"
#include "traffic_trace_rules.h"
#include "vehicle_pool.h"

namespace {

using population_rules::Player;
using traffic_trace_rules::OwnerAreaCounts;

constexpr uint64_t kLineIntervalMs = 60000;
constexpr size_t kMaxPlayers = population_rules::kMaxTrafficPlayers;
constexpr size_t kMinOwnerSlots = 2;

void* g_addOriginal = nullptr;
void* g_removeOriginal = nullptr;
std::mutex g_mutex;
OwnerAreaCounts g_spawned;
OwnerAreaCounts g_despawned;
rate_limit::RateLimit g_lineLimit(kLineIntervalMs);

// The owned vehicles of the traffic list that live farther from this machine's player than from another, for the minute line.
size_t countFarOwned(traffic_trace_rules::FarOwned& example) {
    Player players[kMaxPlayers];
    uint8_t localSlot = 0;
    const size_t playerCount = traffic_list::players(players, kMaxPlayers, localSlot);
    if (playerCount == 0 || players[0].slot != localSlot) return 0;
    traffic_list::Vehicle vehicles[sr2::kTrafficListWalkLimit];
    const size_t vehicleCount = traffic_list::take(vehicles, sr2::kTrafficListWalkLimit);
    size_t farCount = 0;
    for (size_t i = 0; i < vehicleCount; ++i) {
        if (!vehicles[i].owned) continue;
        const traffic_trace_rules::FarOwned found = traffic_trace_rules::farOwned(vehicles[i].position, players[0], players + 1, playerCount - 1);
        if (!found.isFar) continue;
        if (farCount++ == 0) example = found;
    }
    return farCount;
}

// The line once a minute while the traffic list changed; the counts are those since the last line.
void writeLine() {
    traffic_trace_rules::FarOwned example;
    const size_t farCount = countFarOwned(example);
    logger::write("%s", traffic_trace_rules::trafficLine(g_spawned, g_despawned, farCount, example).c_str());
    g_spawned.clear();
    g_despawned.clear();
}

// An entry of the traffic list is added or removed (eax = the entry, its vehicle's handle at +8): counted by the vehicle's owner and
// the player nearest to it.
void note(GuestFrame* frame, OwnerAreaCounts& counts) {
    FpuGuard guard;
    const uintptr_t object = remote_players::objectOfHandle(readDword(frame->regs.eax + sr2::kTrafficEntryHandleOffset));
    if (object == 0) return;
    Player players[kMaxPlayers];
    uint8_t localSlot = 0;
    const size_t playerCount = traffic_list::players(players, kMaxPlayers, localSlot);
    const uintptr_t record = readDword(object + sr2::kObjectNetRecordOffset);
    const uint8_t owner = record ? readByte(record + sr2::kRecordOwnerOffset) : localSlot;
    uint8_t area = owner;
    float apart = 0;
    const partner_rules::Vec3 position = remote_players::positionOf(object);
    traffic_trace_rules::nearestPlayer({position.x, position.y, position.z}, players, playerCount, area, apart);
    uint32_t events = 0;
    std::lock_guard<std::mutex> lock(g_mutex);
    counts.add(owner, area);
    if (g_lineLimit.allow(GetTickCount64(), events)) writeLine();
    vehicle_pool::traceGuard();
}

void __cdecl traceTrafficAdd(GuestFrame* frame) { note(frame, g_spawned); }
void __cdecl traceTrafficRemove(GuestFrame* frame) { note(frame, g_despawned); }

TRACE_ENTRY_STUB(trafficAddStub, traceTrafficAdd, g_addOriginal)
TRACE_ENTRY_STUB(trafficRemoveStub, traceTrafficRemove, g_removeOriginal)

// The vehicles of the object table by owner: a vehicle is an object of type 5 whose handle names its table entry.
void countVehicles(vehicle_census_rules::Census& census, uint8_t localSlot) {
    for (size_t index = 0; index < sr2::kObjectTableEntries; ++index) {
        const uintptr_t object = readDword(sr2::kObjectTable + index * sr2::kObjectTableStride);
        if (object == 0 || (readDword(object + sr2::kObjectHandleOffset) & sr2::kHandleIndexMask) != index) continue;
        if (readDword(object + sr2::kObjectTypeOffset) != sr2::kObjectTypeVehicle) continue;
        const uintptr_t record = readDword(object + sr2::kObjectNetRecordOffset);
        const uint8_t owner = record ? readByte(record + sr2::kRecordOwnerOffset) : localSlot;
        if (owner < traffic_trace_rules::kSlots) ++census.byOwner[owner];
        if (owner == localSlot) ++census.own;
    }
}

uint32_t freeSlotPointers() {
    uint32_t free = 0;
    for (uint32_t i = 0; i < vehicle_pool::capacity(); ++i) {
        if (readDword(vehicle_pool::slotPointersAddress() + i * sizeof(uint32_t)) == 0) ++free;
    }
    return free;
}

}  // namespace

namespace traffic_trace {

vehicle_census_rules::Census vehicleCensus() {
    using vehicle_pool_sites::TableId;
    vehicle_census_rules::Census census;
    census.capacity = vehicle_pool::capacity();
    census.inUse = readDword(sr2::kVehicleCount);
    uint8_t localSlot = 0;
    traffic_list::localSlot(localSlot);
    countVehicles(census, localSlot);
    remote_players::Snapshot snapshot;
    remote_players::takeMembers(snapshot);
    census.ownerSlots = kMinOwnerSlots;
    for (size_t i = 0; i < snapshot.memberCount; ++i) census.ownerSlots = std::max<size_t>(census.ownerSlots, snapshot.members[i].slot + size_t{1});
    traffic_list::Vehicle traffic[sr2::kTrafficListWalkLimit];
    const size_t trafficCount = traffic_list::take(traffic, sr2::kTrafficListWalkLimit);
    census.traffic = static_cast<uint32_t>(trafficCount);
    for (size_t i = 0; i < trafficCount; ++i) census.ownTraffic += traffic[i].owned ? 1 : 0;
    const population::LastQuota quota = population::lastQuota();
    census.quota = quota.quota.quota;
    census.leads = quota.quota.leads;
    census.players = quota.quota.players;
    census.class1Count = readDword(sr2::kCategoryClass1Count);
    census.class1Limit = readDword(sr2::kCategoryClass1Limit);
    census.netRecords = readDword(sr2::kNetRecordsInUse);
    const TableId byteTables[] = {TableId::T1, TableId::T2, TableId::T4, TableId::T6, TableId::T7, TableId::T8};
    size_t row = 0;
    for (const TableId id : byteTables) {
        const uint32_t used = readByte(vehicle_pool::countByteAddress(id));
        census.sideTables[row++] = {vehicle_pool_sites::tableOf(id).name, used < census.capacity ? census.capacity - used : 0};
    }
    census.sideTables[row++] = {"CN", readDword(sr2::kCategoryFreeNodes)};
    census.sideTables[row] = {"CU", freeSlotPointers()};
    return census;
}

partner_thunks::Binding bind(partner_sites::Thunk thunk) {
    switch (thunk) {
        case partner_sites::Thunk::TraceTrafficAdd: return {reinterpret_cast<void*>(&trafficAddStub), &g_addOriginal};
        case partner_sites::Thunk::TraceTrafficRemove: return {reinterpret_cast<void*>(&trafficRemoveStub), &g_removeOriginal};
        default: return {nullptr, nullptr};
    }
}

}  // namespace traffic_trace
