#include "vehicle_trace.h"

#include <mutex>

#include "log.h"
#include "memory.h"
#include "remote_players.h"
#include "spawn_budget_rules.h"
#include "sr2_addresses.h"
#include "vehicle_pool.h"

namespace {

// The vehicle class create 0x873F30: thiscall(out handle*, offset*, payload, net id, owner slot), the callee pops 5 arguments.
using VehicleCreateFn = uint32_t(__thiscall*)(void*, uint32_t*, int*, const uint8_t*, uint32_t, uint32_t);

// The vehicle class's apply 0x878A90 (vtable +0x48): thiscall(this = the class handler, vehicle handle, payload, offset*),
// the callee pops 12 bytes. 0x804710 calls it with ecx = kRecordClassTable[class] and the record's handle (+4) first.
using VehicleApplyFn = uint32_t(__thiscall*)(void*, uint32_t, uint32_t, uint32_t);

constexpr uint32_t kNetIdMask = 0xFFFF;

void* g_createOriginal = nullptr;
void* g_applyOriginal = nullptr;
std::mutex g_mutex;
vehicle_trace_rules::Totals g_totals;
vehicle_trace_rules::Attempts g_attempts;
vehicle_trace_rules::Attempts g_dropped;

vehicle_trace_rules::CreateState createState() {
    return {readDword(sr2::kVehicleCreationFreeHead), readDword(sr2::kVehicleFreeHead), readDword(sr2::kCategoryClass1Count),
            readDword(sr2::kCategoryClass1Limit), readDword(sr2::kCategoryFreeNodes)};
}

// VT1, entered through the vtable slot with ecx = the class handler. A create that left no handle failed; the game tries
// again, so the first failure of a net id is logged with its reason and the success that follows with the attempts it took.
uint32_t __fastcall traceRemoteVehicle(void* handler, void*, uint32_t* out, int* offset, const uint8_t* payload, uint32_t netId,
                                       uint32_t ownerSlot) {
    const uint32_t result = reinterpret_cast<VehicleCreateFn>(g_createOriginal)(handler, out, offset, payload, netId, ownerSlot);
    const uint16_t id = static_cast<uint16_t>(netId & kNetIdMask);
    const bool created = out && readDword(reinterpret_cast<uintptr_t>(out)) != 0;
    const vehicle_trace_rules::CreateState state = createState();
    const vehicle_trace_rules::Failure failure = vehicle_trace_rules::classify(state);
    std::lock_guard<std::mutex> lock(g_mutex);
    g_totals.record(created, failure);
    if (created) {
        const uint32_t failedBefore = g_attempts.created(id);
        if (failedBefore > 0) logger::write("trace: vehicles: remote vehicle id 0x%04X created after %u attempts", id, failedBefore + 1);
    } else if (g_attempts.failed(id) == 1) {
        logger::write("trace: vehicles: remote vehicle id 0x%04X of slot %u not created (%s), free vehicles %u/%u, creation data %u/%zu, class 1 %u/%u", id,
                      ownerSlot, vehicle_trace_rules::name(failure), vehicle_trace::freeVehicles(), vehicle_pool::capacity(),
                      vehicle_trace::freeCreationData(), sr2::kVehicleCreationPool, state.class1Count, state.class1Limit);
    }
    vehicle_pool::traceGuard();
    return result;
}

// VT2, entered through the vehicle class's vtable +0x48 with ecx = the class handler and the vehicle's handle first. The apply
// destroys a vehicle whose stream container is not loaded on this machine; the handle then no longer names it. Its owner and
// net id are read before.
uint32_t __fastcall traceVehicleApply(void* handler, void*, uint32_t handle, uint32_t payload, uint32_t offset) {
    const uintptr_t object = remote_players::objectOfHandle(handle);
    const uint16_t id = remote_players::netIdOf(object);
    const uintptr_t record = object ? readDword(object + sr2::kObjectNetRecordOffset) : 0;
    const uint32_t ownerSlot = record ? readByte(record + sr2::kRecordOwnerOffset) : 0;
    const uint32_t result = reinterpret_cast<VehicleApplyFn>(g_applyOriginal)(handler, handle, payload, offset);
    if (object != 0 && remote_players::objectOfHandle(handle) != object) {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_totals.recordDropped();
        if (g_dropped.failed(id) == 1) {
            logger::write("trace: vehicles: remote vehicle id 0x%04X of slot %u dropped at apply (stream container not loaded)", id, ownerSlot);
        }
    }
    vehicle_pool::traceGuard();
    return result;
}

}  // namespace

namespace vehicle_trace {

vehicle_trace_rules::Totals totals() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_totals;
}

uint32_t freeVehicles() {
    const uint32_t inUse = readDword(sr2::kVehicleCount);
    return inUse < vehicle_pool::capacity() ? vehicle_pool::capacity() - inUse : 0;
}

uint32_t freeCreationData() {
    return spawn_budget_rules::countCircular(readDword(sr2::kVehicleCreationFreeHead), static_cast<uint32_t>(sr2::kVehicleCreationPool),
                                             [](uintptr_t node) { return readDword(node + sr2::kVehicleCreationLinkOffset); });
}

partner_thunks::Binding bind(partner_sites::Thunk thunk) {
    switch (thunk) {
        case partner_sites::Thunk::TraceRemoteVehicle: return {reinterpret_cast<void*>(&traceRemoteVehicle), &g_createOriginal};
        case partner_sites::Thunk::TraceVehicleApply: return {reinterpret_cast<void*>(&traceVehicleApply), &g_applyOriginal};
        default: return {nullptr, nullptr};
    }
}

}  // namespace vehicle_trace
