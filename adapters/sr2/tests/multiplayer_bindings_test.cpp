// Every milestone 9 site has a thunk, and no two hook sites share the slot that receives a trampoline (no game).
#include <cstdio>
#include <set>
#include <vector>

#include "../src/multiplayer_sites.h"
#include "../src/multiplayer_thunks.h"

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
    bool allBound = true;
    bool hooksHaveSlots = true;
    std::set<void**> slots;
    bool slotsAreDistinct = true;
    std::vector<partner_sites::Site> sites;
    for (const multiplayer_sites::Phase& phase : multiplayer_sites::kPhases) sites.insert(sites.end(), phase.sites.begin(), phase.sites.end());
    for (const auto table : multiplayer_sites::kScriptTraces) sites.insert(sites.end(), table.begin(), table.end());
    for (const partner_sites::Site& site : sites) {
        if (site.kind == partner_sites::Kind::Bytes) continue;
        const partner_thunks::Binding binding = multiplayer_thunks::bindingOf(site.thunk);
        if (!binding.entry) {
            std::printf("     %s has no thunk\n", site.id);
            allBound = false;
            continue;
        }
        if (site.kind != partner_sites::Kind::HookEntry && site.kind != partner_sites::Kind::PointerSwap) continue;
        if (site.thunk == partner_sites::Thunk::QueueAppendFunction) continue;  // replaces a leaf function whole
        hooksHaveSlots = hooksHaveSlots && binding.original;
        slotsAreDistinct = slotsAreDistinct && slots.insert(binding.original).second;
    }
    check(allBound, "every site has a thunk");
    check(hooksHaveSlots, "every hook has a slot for its trampoline");
    check(slotsAreDistinct, "no two hooks share a trampoline slot");
    return g_failures == 0 ? 0 : 1;
}
