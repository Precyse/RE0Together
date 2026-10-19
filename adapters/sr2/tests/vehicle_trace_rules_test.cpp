// The remote vehicle trace rules of milestone 12 VT1 (no game): the reason of a failed create, the totals, and the attempts
// per net id.
#include <cstdio>
#include <string>

#include "../src/vehicle_trace_rules.h"

namespace {

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_failures;
}

}  // namespace

int main() {
    using namespace vehicle_trace_rules;

    check(classify(0, 0x1000) == Failure::CreationData, "no creation data record: that is the reason, the create asks for it first");
    check(classify(0, 0) == Failure::CreationData, "both lists empty: the creation data");
    check(classify(0x2000, 0) == Failure::Pool, "records free, no vehicle in the pool: the pool");
    check(classify(0x2000, 0x1000) == Failure::Other, "both free: a category slot or the streaming");
    check(std::string(name(Failure::Pool)) == "pool" && std::string(name(Failure::CreationData)) == "creation data" &&
              std::string(name(Failure::Other)) == "other",
          "the names the trace line uses");

    Totals totals;
    totals.record(true, Failure::Other);
    totals.record(false, Failure::Pool);
    totals.record(false, Failure::Pool);
    totals.record(false, Failure::CreationData);
    totals.record(false, Failure::Other);
    check(totals.creates == 5 && totals.failed == 4 && totals.pool == 2 && totals.creationData == 1 && totals.other == 1,
          "the totals count every create, the failed ones and their reasons; a create that worked adds no reason");

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
