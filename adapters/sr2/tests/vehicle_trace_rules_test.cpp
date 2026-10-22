// The remote vehicle trace rules of milestone 12 VT1 (no game): the reason of a failed create, the totals, and the attempts
// per net id.
#include <cstdio>
#include <string>

#include "../src/vehicle_trace_rules.h"
#include "check.h"

int main() {
    using namespace vehicle_trace_rules;

    constexpr uint32_t kRecord = 0x2000;
    constexpr uint32_t kVehicle = 0x1000;
    constexpr uint32_t kRoomForClass1 = 4;
    constexpr uint32_t kLimit = 12;
    constexpr uint32_t kNodes = 20;
    check(classify({0, kVehicle, kRoomForClass1, kLimit, kNodes}) == Failure::CreationData, "no creation data record: that is the reason, the create asks for it first");
    check(classify({0, 0, kLimit, kLimit, 0}) == Failure::CreationData, "everything empty: the creation data");
    check(classify({kRecord, 0, kLimit, kLimit, 0}) == Failure::Pool, "records free, no vehicle in the pool: the pool before the category");
    check(classify({kRecord, kVehicle, kLimit, kLimit, kNodes}) == Failure::Category, "class 1 at its limit: the category");
    check(classify({kRecord, kVehicle, kLimit + 1, kLimit, kNodes}) == Failure::Category, "class 1 over its limit (owners racing): the category");
    check(classify({kRecord, kVehicle, kRoomForClass1, kLimit, 0}) == Failure::Category, "no free category node: the category");
    check(classify({kRecord, kVehicle, kRoomForClass1, kLimit, kNodes}) == Failure::Other, "everything has room: the streaming or something unknown");
    check(std::string(name(Failure::Pool)) == "pool" && std::string(name(Failure::CreationData)) == "creation data" &&
              std::string(name(Failure::Category)) == "category" && std::string(name(Failure::Other)) == "other",
          "the names the trace line uses");

    Totals totals;
    totals.record(true, Failure::Other);
    totals.record(false, Failure::Pool);
    totals.record(false, Failure::Pool);
    totals.record(false, Failure::CreationData);
    totals.record(false, Failure::Category);
    totals.record(false, Failure::Other);
    totals.recordDropped();
    check(totals.creates == 6 && totals.failed == 5 && totals.pool == 2 && totals.creationData == 1 && totals.category == 1 &&
              totals.other == 1 && totals.droppedAtApply == 1,
          "the totals count every create, the failed ones and their reasons; a create that worked adds no reason; a drop at apply is its own count");

    Attempts attempts;
    check(attempts.failed(7) == 1 && attempts.failed(7) == 2 && attempts.failed(8) == 1, "a failed attempt counts per net id, the first is 1");
    check(attempts.created(7) == 2 && attempts.created(7) == 0, "a create after 2 failures reports them and forgets the id");
    check(attempts.created(99) == 0, "a create that never failed reports nothing");
    check(attempts.failed(7) == 1, "an id that was created starts again");

    Attempts full;
    for (uint32_t id = 0; id < kMaxTrackedIds; ++id) full.failed(static_cast<uint16_t>(id));
    full.failed(1000);
    check(full.failed(0) == 1 && full.failed(2) == 2, "a full set forgets its oldest id and keeps the others");
    return g_failures == 0 ? 0 : 1;
}
