// Checks the session load from the title: where a pending start takes the title and what a finished load leads to
// (no game). Exit 0 when every check passes.
#include <cstdio>

#include "../src/title_load_rule.h"

namespace {

using namespace title_load_rule;

int g_failures = 0;

void check(bool ok, const char* what) {
    if (ok) return;
    std::printf("FAIL: %s\n", what);
    ++g_failures;
}

constexpr uint32_t kPressStart = 6;

void testTakesTitle() {
    check(takesTitle(kStateMainMenuInput, kStateMainMenuInput, true), "the settled main menu");
    check(!takesTitle(kStateMainMenuInput, kStateMainMenuInput, false), "not while its sequence or the fade runs");
    check(takesTitle(kStateOptionsWait, kStateMainMenuOpen, false), "the frame the Options screen closed");
    check(!takesTitle(kStateOptionsWait, kStateOptionsWait, false), "not while the Options screen is open");
    check(!takesTitle(kPressStart, kPressStart, true), "never a boot screen");
    check(!takesTitle(kStateMainMenuOpen, kStateMainMenuOpen, true), "not the menu opening (a boot dialog may still answer)");
}

void testAfterLoad() {
    const Order host{3, true, std::nullopt};
    const Order guest{3, false, 0xabcd};
    check(afterLoad(host, 0, 0x1111, 1, 0) == Verdict::Commit, "the host starts its own bytes");
    check(afterLoad(guest, 0, 0xabcd, 1, 0) == Verdict::Commit, "a guest starts the host's bytes");
    // The guard: a guest never starts a world that differs from the host's.
    check(afterLoad(guest, 0, 0x1111, 1, 0) == Verdict::Retry, "another copy waits for a newer one");
    check(afterLoad(guest, 0, std::nullopt, 1, 0) == Verdict::Retry, "an unreadable block is no match");
    check(afterLoad(guest, 0, 0x1111, 9, kFileWaitMs) == Verdict::Fail, "after the wait the guest gives up");
    check(afterLoad(host, 5, 0x1111, 1, 0) == Verdict::Retry, "a failed load is asked again");
    check(afterLoad(host, 5, 0x1111, kMaxRequestTries, 0) == Verdict::Fail, "a load that keeps failing gives up");
    check(!requestGivesUp(kRequestRefusedMs - 1) && requestGivesUp(kRequestRefusedMs), "a refused request is bounded");
}

}  // namespace

int main() {
    testTakesTitle();
    testAfterLoad();
    if (g_failures == 0) std::printf("PASS\n");
    return g_failures == 0 ? 0 : 1;
}
