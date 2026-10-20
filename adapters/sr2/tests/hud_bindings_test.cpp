// Every milestone 16 wave 2 site has a thunk, and no two hook sites share the slot that receives a trampoline (no game).
#include <cstdio>
#include <set>

#include "../src/hud_remotes.h"
#include "../src/hud_sites.h"
#include "../src/partner_thunks.h"
#include "../src/waypoint_scope.h"
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
    for (const auto bind : {&hud_remotes::bind, &waypoint_scope::bind, &partner_thunks::bindCalls}) {
        const partner_thunks::Binding binding = bind(thunk);
        if (binding.entry) return binding;
    }
    return {nullptr, nullptr};
}

}  // namespace

int main() {
    bool allBound = true;
    bool hooksHaveSlots = true;
    bool slotsAreDistinct = true;
    std::set<void**> slots;
    for (const hud_sites::Set& set : hud_sites::kSets) {
        for (const partner_sites::Site& site : set.sites) {
            if (site.kind == partner_sites::Kind::Bytes) continue;
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
    }
    check(allBound, "every written row has a thunk");
    check(hooksHaveSlots, "every hook has a slot for its trampoline");
    check(slotsAreDistinct, "no two hooks share a trampoline slot");
    return g_failures == 0 ? 0 : 1;
}
