// Every Trail Blazing site that writes code has a thunk, and none of them is a hook (no game).
#include <cstdio>

#include "../src/trail_blazing.h"
#include "../src/trail_blazing_sites.h"

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
    bool noHooks = true;
    bool checkRowsUnbound = true;
    for (const trail_blazing_sites::Set& set : trail_blazing_sites::kSets) {
        for (const partner_sites::Site& site : set.sites) {
            const partner_thunks::Binding binding = trail_blazing::bindingOf(site.thunk);
            if (site.kind == partner_sites::Kind::Bytes) {
                checkRowsUnbound = checkRowsUnbound && !binding.entry;
                continue;
            }
            if (!binding.entry) {
                std::printf("     %s has no thunk\n", site.id);
                allBound = false;
            }
            noHooks = noHooks && site.kind != partner_sites::Kind::HookEntry && !binding.original;
        }
    }
    check(allBound, "every written site has a thunk");
    check(noHooks, "no site is a hook and no binding holds a trampoline slot");
    check(checkRowsUnbound, "a check row has no thunk");
    return g_failures == 0 ? 0 : 1;
}
