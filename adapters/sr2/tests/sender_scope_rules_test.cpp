// The sender scope rules of milestone 9 (no game): which net id a look message must name.
#include <cstdio>

#include "../src/sender_scope_rules.h"

namespace {

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_failures;
}

}  // namespace

int main() {
    using sender_scope_rules::kNoNetId;
    using sender_scope_rules::lookNetId;

    check(lookNetId(0x1002, 0x1002) == 0x1002, "a payload that names the sender's human stays as it is (the 2-player case)");
    check(lookNetId(0x3004, 0x2004) == 0x2004, "a payload that names another player is made to name the sender's human");
    check(lookNetId(0x2004, kNoNetId) == 0x2004, "a sender with no net id yet keeps the payload's");
    check(lookNetId(kNoNetId, 0x2004) == 0x2004, "an empty payload id is filled in");
    return g_failures == 0 ? 0 : 1;
}
