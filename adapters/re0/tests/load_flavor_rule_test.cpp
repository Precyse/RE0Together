// Checks which common messages the load-in skip leaves unshown. Exit 0 when every check passes.
#include <cstdio>

#include "../src/load_flavor_rule.h"

namespace {

using namespace load_flavor_rule;

int g_failures = 0;

void check(bool ok, const char* what) {
    if (ok) return;
    std::printf("FAIL: %s\n", what);
    ++g_failures;
}

}  // namespace

int main() {
    check(skips(0x58) && skips(0x5a) && skips(0x5f), "every load-in line is skipped");
    check(!skips(0x57) && !skips(0x60), "the neighbours (a colon, the Time label) still show");
    check(!skips(0x1c) && !skips(0x1d) && !skips(0x0c), "the save point's messages still show");
    if (g_failures == 0) std::printf("PASS\n");
    return g_failures == 0 ? 0 : 1;
}
