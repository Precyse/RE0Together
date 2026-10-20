// Every milestone 16 W1 site has a thunk, and no two hook sites share the slot that receives a trampoline (no game).
#include <cstdio>
#include <set>
#include <span>
#include <vector>

#include "../src/outfit_sync.h"
#include "../src/outfit_sync_sites.h"
#include "../src/outfit_sync_trace.h"
#include "../src/partner_thunks.h"
#include "check.h"

namespace logger {
void write(const char*, ...) {}
void writeUnlessRepeated(const char*, ...) {}
}  // namespace logger

namespace debug_stats {
void setError(const char*, ...) {}
}  // namespace debug_stats

namespace {

partner_thunks::Binding bindingOf(partner_sites::Thunk thunk) {
    for (const auto bind : {&outfit_sync::bind, &outfit_sync_trace::bind, &partner_thunks::bindCalls}) {
        const partner_thunks::Binding binding = bind(thunk);
        if (binding.entry) return binding;
    }
    return {nullptr, nullptr};
}

}  // namespace

int main() {
    std::vector<partner_sites::Site> sites;
    for (const auto table : {std::span<const partner_sites::Site>(outfit_sync_sites::kLookSync), std::span<const partner_sites::Site>(outfit_sync_sites::kItemWear),
                             std::span<const partner_sites::Site>(outfit_sync_sites::kWeaponSlots), std::span<const partner_sites::Site>(outfit_sync_sites::kTraces)}) {
        sites.insert(sites.end(), table.begin(), table.end());
    }
    bool allBound = true;
    bool hooksHaveSlots = true;
    bool slotsAreDistinct = true;
    std::set<void**> slots;
    for (const partner_sites::Site& site : sites) {
        const partner_thunks::Binding binding = bindingOf(site.thunk);
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
