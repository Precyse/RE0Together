// Checks the world epoch rules (no game): the host's counter never shows 0, and a guest's message is for the current
// world only with the host's epoch as its stamp. Exit 0 when every check passes.
#include <cstdio>

#include "../src/world_epoch_rule.h"

namespace {

namespace rule = world_epoch_rule;

constexpr uint16_t kHostEpoch = 4;
int g_failures = 0;

void check(bool ok, const char* what) {
    if (ok) return;
    std::printf("FAIL: %s\n", what);
    ++g_failures;
}

void testCounter() {
    check(rule::next(rule::kFirstEpoch) == rule::kFirstEpoch + 1, "a load advances the epoch by one");
    check(rule::next(UINT16_MAX) == rule::kFirstEpoch, "the counter wraps past 0, which is never an epoch");
    check(rule::kNone != rule::kFirstEpoch, "the first epoch is not the unstamped value");
}

void testJudgement() {
    check(rule::current(kHostEpoch, kHostEpoch), "a message of the host's world is current");
    check(!rule::current(kHostEpoch - 1, kHostEpoch), "a request made before the host's load is stale");
    check(!rule::current(kHostEpoch + 1, kHostEpoch), "and so is a stamp the host has not reached");
    check(!rule::current(rule::kNone, kHostEpoch), "a guest that has not adopted a snapshot is not in the host's world");
}

void testLoadSequence() {
    uint16_t host = rule::kFirstEpoch;
    uint16_t guestStamp = host;  // the guest adopted the host's snapshot
    check(rule::current(guestStamp, host), "before any load the guest's stamp is current");
    host = rule::next(host);  // the host loads
    check(!rule::current(guestStamp, host), "a request sent before the host's load is refused once the host loaded");
    guestStamp = host;  // the guest loads, asks for a snapshot and adopts the new epoch
    check(rule::current(guestStamp, host), "after the guest adopts the new snapshot its requests count again");
}

}  // namespace

int main() {
    testCounter();
    testJudgement();
    testLoadSequence();
    if (g_failures == 0) std::printf("world_epoch_rule_test: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
