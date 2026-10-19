#include "vehicle_trace.h"

#include <mutex>

#include "log.h"
#include "memory.h"
#include "spawn_budget_rules.h"
#include "sr2_addresses.h"

namespace {

// The vehicle class create 0x873F30: thiscall(out handle*, offset*, payload, net id, owner slot), the callee pops 5 arguments.
using VehicleCreateFn = uint32_t(__thiscall*)(void*, uint32_t*, int*, const uint8_t*, uint32_t, uint32_t);

constexpr uint32_t kNetIdMask = 0xFFFF;

void* g_createOriginal = nullptr;
std::mutex g_mutex;
vehicle_trace_rules::Totals g_totals;
vehicle_trace_rules::Attempts g_attempts;

// VT1, entered through the vtable slot with ecx = the class handler. A create that left no handle failed; the game tries
// again, so the first failure of a net id is logged with its reason and the success that follows with the attempts it took.
uint32_t __fastcall traceRemoteVehicle(void* handler, void*, uint32_t* out, int* offset, const uint8_t* payload, uint32_t netId,
                                       uint32_t ownerSlot) {
    const uint32_t result = reinterpret_cast<VehicleCreateFn>(g_createOriginal)(handler, out, offset, payload, netId, ownerSlot);
    const uint16_t id = static_cast<uint16_t>(netId & kNetIdMask);
    const bool created = out && readDword(reinterpret_cast<uintptr_t>(out)) != 0;
    const vehicle_trace_rules::Failure failure =
        vehicle_trace_rules::classify(readDword(sr2::kVehicleCreationFreeHead), readDword(sr2::kVehicleFreeHead));
    std::lock_guard<std::mutex> lock(g_mutex);
    g_totals.record(created, failure);
    if (created) {
        const uint32_t failedBefore = g_attempts.created(id);
        if (failedBefore > 0) logger::write("trace: vehicles: remote vehicle id 0x%04X created after %u attempts", id, failedBefore + 1);
    } else if (g_attempts.failed(id) == 1) {
        logger::write("trace: vehicles: remote vehicle id 0x%04X of slot %u not created (%s), free vehicles %u/%zu, creation data %u/%zu", id,
                      ownerSlot, vehicle_trace_rules::name(failure), vehicle_trace::freeVehicles(), sr2::kVehiclePool,
                      vehicle_trace::freeCreationData(), sr2::kVehicleCreationPool);
    }
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
    return inUse < sr2::kVehiclePool ? static_cast<uint32_t>(sr2::kVehiclePool) - inUse : 0;
}

uint32_t freeCreationData() {
    return spawn_budget_rules::countCircular(readDword(sr2::kVehicleCreationFreeHead), static_cast<uint32_t>(sr2::kVehicleCreationPool),
                                             [](uintptr_t node) { return readDword(node + sr2::kVehicleCreationLinkOffset); });
}

partner_thunks::Binding bind(partner_sites::Thunk thunk) {
    switch (thunk) {
        case partner_sites::Thunk::TraceRemoteVehicle: return {reinterpret_cast<void*>(&traceRemoteVehicle), &g_createOriginal};
        default: return {nullptr, nullptr};
    }
}

}  // namespace vehicle_trace
