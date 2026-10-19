// Every milestone 7 site, every milestone 16 wave 2 diversion site and every milestone 17 wave 3 Crowd Control site has a thunk, and
// no two hook sites share the slot that receives a trampoline (no game).
#include <cstdio>
#include <set>
#include <span>
#include <vector>

#include "../src/activity_sites.h"
#include "../src/activity_thunks.h"
#include "../src/crowd_control_sites.h"
#include "../src/crowd_control_thunks.h"
#include "../src/diversion_sites.h"
#include "../src/diversion_thunks.h"

namespace logger {
void write(const char*, ...) {}
void writeUnlessRepeated(const char*, ...) {}
}  // namespace logger

namespace {

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_failures;
}

partner_thunks::Binding bindingOf(partner_sites::Thunk thunk) {
    for (const auto bind : {&activity_thunks::bindFramework, &activity_thunks::bindCredit, &activity_thunks::bindGames,
                            &partner_thunks::bindCalls, &diversion_thunks::bind, &crowd_control_thunks::bind}) {
        const partner_thunks::Binding binding = bind(thunk);
        if (binding.entry) return binding;
    }
    return {nullptr, nullptr};
}

}  // namespace

int main() {
    bool allBound = true;
    bool hooksHaveSlots = true;
    std::set<void**> slots;
    bool slotsAreDistinct = true;
    std::vector<std::span<const partner_sites::Site>> tables = {activity_sites::kPhase1, activity_sites::kPhase2, activity_sites::kPhase3};
    for (const diversion_sites::Set& set : diversion_sites::kSets) tables.push_back(set.sites);
    for (const crowd_control_sites::Set& set : crowd_control_sites::kSets) tables.push_back(set.sites);
    for (const auto phase : tables) {
        for (const partner_sites::Site& site : phase) {
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
    check(allBound, "every site has a thunk");
    check(hooksHaveSlots, "every hook has a slot for its trampoline");
    check(slotsAreDistinct, "no two hooks share a trampoline slot");
    return g_failures == 0 ? 0 : 1;
}
