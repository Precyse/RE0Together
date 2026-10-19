// Every milestone 17 wave 3 row has a thunk, and the two pointer swaps have a slot for the game's own handler (no game).
#include <cstdio>
#include <string_view>

#include "../src/world_walks.h"
#include "../src/world_walks_sites.h"

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
    bool swapsHaveSlots = true;
    bool slotsAreDistinct = true;
    void** seen[2] = {nullptr, nullptr};
    size_t seenCount = 0;
    for (const world_walks_sites::Set& set : world_walks_sites::kSets) {
        for (const partner_sites::Site& site : set.sites) {
            if (site.kind == partner_sites::Kind::Bytes) continue;
            const partner_thunks::Binding binding = world_walks::bindingOf(site.thunk);
            if (!binding.entry) {
                std::printf("     %s has no thunk\n", site.id);
                allBound = false;
                continue;
            }
            if (site.kind != partner_sites::Kind::PointerSwap) continue;
            swapsHaveSlots = swapsHaveSlots && binding.original != nullptr;
            for (size_t i = 0; i < seenCount; ++i) slotsAreDistinct = slotsAreDistinct && seen[i] != binding.original;
            if (seenCount < std::size(seen)) seen[seenCount++] = binding.original;
        }
    }
    check(allBound, "every written row has a thunk");
    check(swapsHaveSlots, "PD3 and CN1 have a slot for the game's handler");
    check(slotsAreDistinct && seenCount == 2, "the two pointer swaps do not share a slot");
    return g_failures == 0 ? 0 : 1;
}
