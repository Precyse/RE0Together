// Every vehicle seating site has a thunk, and no two hook sites share the slot that receives a trampoline (no game).
#include <cstdio>
#include <set>
#include <span>
#include <vector>

#include "../src/partner_thunks.h"
#include "../src/vehicle_seating.h"
#include "../src/vehicle_seating_sites.h"

namespace logger {
void write(const char*, ...) {}
void writeUnlessRepeated(const char*, ...) {}
}  // namespace logger

namespace debug_stats {
void setError(const char*, ...) {}
}  // namespace debug_stats

namespace {

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_failures;
}

}  // namespace

int main() {
    std::vector<partner_sites::Site> sites;
    for (const auto table : {std::span<const partner_sites::Site>(vehicle_seating_sites::kTrafficking),
                             std::span<const partner_sites::Site>(vehicle_seating_sites::kCheckpoint),
                             std::span<const partner_sites::Site>(vehicle_seating_sites::kMissionExit),
                             std::span<const partner_sites::Site>(vehicle_seating_sites::kTrace)}) {
        sites.insert(sites.end(), table.begin(), table.end());
    }
    bool allBound = true;
    bool hooksHaveSlots = true;
    bool slotsAreDistinct = true;
    std::set<void**> slots;
    for (const partner_sites::Site& site : sites) {
        const partner_thunks::Binding binding = vehicle_seating::bind(site.thunk);
        if (!binding.entry) {
            std::printf("     %s has no thunk\n", site.id);
            allBound = false;
            continue;
        }
        if (site.kind != partner_sites::Kind::HookEntry) continue;
        hooksHaveSlots = hooksHaveSlots && binding.original;
        slotsAreDistinct = slotsAreDistinct && slots.insert(binding.original).second;
    }
    check(allBound, "every site has a thunk");
    check(hooksHaveSlots, "every hook has a slot for its trampoline");
    check(slotsAreDistinct, "no two hooks share a trampoline slot");
    return g_failures == 0 ? 0 : 1;
}
