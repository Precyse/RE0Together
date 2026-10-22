#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

#include "sr2_addresses.h"
#include "traffic_trace_rules.h"

// The vehicle part of the pool census line (milestone 15 B1, trace=1, unit tested; docs/SR2_NOTES.md, "Pool census"): the
// vehicles in use by owner, the traffic and its quota, the net records and the free entries of the tables sized with the pool.
namespace vehicle_census_rules {

constexpr size_t kSideTables = 8;  // T1 T2 T4 T6 T7 T8 CN CU

struct SideTable {
    const char* name;
    uint32_t free;
};

struct Census {
    uint32_t inUse = 0;
    uint32_t capacity = 0;
    uint32_t own = 0;
    uint32_t byOwner[traffic_trace_rules::kSlots] = {};
    size_t ownerSlots = 0;  // the slots listed: 0 .. ownerSlots-1
    uint32_t traffic = 0;
    uint32_t ownTraffic = 0;
    int32_t quota = 0;
    size_t leads = 0;
    size_t players = 0;
    uint32_t class1Count = 0;
    uint32_t class1Limit = 0;
    uint32_t netRecords = 0;
    SideTable sideTables[kSideTables] = {};
};

inline std::string text(const Census& census) {
    std::string owners;
    for (size_t slot = 0; slot < census.ownerSlots && slot < traffic_trace_rules::kSlots; ++slot) {
        char part[24];
        std::snprintf(part, sizeof(part), "%s%zu:%u", slot == 0 ? "" : " ", slot, census.byOwner[slot]);
        owners += part;
    }
    std::string tables;
    for (size_t i = 0; i < kSideTables; ++i) {
        char part[40];
        std::snprintf(part, sizeof(part), "%s%s %u/%u", i == 0 ? "" : " ", census.sideTables[i].name, census.sideTables[i].free, census.capacity);
        tables += part;
    }
    char line[640];
    std::snprintf(line, sizeof(line),
                  "vehicles in use %u/%u (own %u, by owner slot %s), traffic %u (own %u, quota %d, leads %zu of %zu, class 1 %u/%u), "
                  "net records %u/%zu, side tables %s",
                  census.inUse, census.capacity, census.own, owners.c_str(), census.traffic, census.ownTraffic, census.quota, census.leads,
                  census.players, census.class1Count, census.class1Limit, census.netRecords, sr2::kNetRecordPool, tables.c_str());
    return line;
}

}  // namespace vehicle_census_rules
