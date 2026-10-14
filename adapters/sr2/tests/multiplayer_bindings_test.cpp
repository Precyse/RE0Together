// Every milestone 9 site has a thunk, and no two hook sites share the slot that receives a trampoline (no game).
#include <cstdio>
#include <set>

#include "../src/multiplayer_sites.h"
#include "../src/multiplayer_thunks.h"

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

}  // namespace

int main() {
    bool allBound = true;
    bool hooksHaveSlots = true;
    std::set<void**> slots;
    bool slotsAreDistinct = true;
    for (const multiplayer_sites::Phase& phase : multiplayer_sites::kPhases) {
        for (const partner_sites::Site& site : phase.sites) {
            if (site.kind == partner_sites::Kind::Bytes) continue;
            const partner_thunks::Binding binding = multiplayer_thunks::bindingOf(site.thunk);

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
