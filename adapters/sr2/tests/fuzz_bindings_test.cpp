// Every written milestone 17 wave 3 FUZZ site has a thunk through the binder the installer uses, every hook has its own slot for its
// trampoline, and the two riot predicates do not share one (no game).
#include <cstdio>
#include <set>
#include <string_view>

#include "../src/fuzz_patches.h"
#include "../src/fuzz_sites.h"
#include "check.h"

namespace logger {
void write(const char*, ...) {}
void writeUnlessRepeated(const char*, ...) {}
}  // namespace logger

namespace debug_stats {
void setError(const char*, ...) {}
}  // namespace debug_stats

namespace {

const partner_sites::Site* siteNamed(std::string_view id) {
    for (const fuzz_sites::Set& set : fuzz_sites::kSets) {
        for (const partner_sites::Site& site : set.sites) {
            if (id == site.id) return &site;
        }
    }
    return nullptr;
}

}  // namespace

int main() {
    bool allBound = true;
    bool hooksHaveSlots = true;
    std::set<void**> slots;
    bool slotsAreDistinct = true;
    for (const fuzz_sites::Set& set : fuzz_sites::kSets) {
        for (const partner_sites::Site& site : set.sites) {
            if (site.kind == partner_sites::Kind::Bytes) continue;
            const partner_thunks::Binding binding = fuzz_patches::bindingOf(site.thunk);
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
    check(allBound, "every written site has a thunk");
    check(hooksHaveSlots, "every hook has a slot for its trampoline");
    check(slotsAreDistinct, "no two hooks share a trampoline slot");

    const partner_sites::Site* riot = siteNamed("FZ15");
    const partner_sites::Site* point = siteNamed("FZ19");
    check(riot && point && riot->kind == partner_sites::Kind::HookEntry && point->kind == partner_sites::Kind::HookEntry &&
              fuzz_patches::bindingOf(riot->thunk).original != fuzz_patches::bindingOf(point->thunk).original,
          "FZ15 and FZ19 are hooks with their own trampoline slots");
    return g_failures == 0 ? 0 : 1;
}
