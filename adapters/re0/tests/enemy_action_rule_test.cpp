// Checks the enemy animation match rule (no game). Exit 0 when every check passes.
#include <cstdio>

#include "../src/enemy_action_rule.h"

namespace {

namespace rule = enemy_action_rule;

int g_failures = 0;

void check(bool ok, const char* what) {
    if (ok) return;
    std::printf("FAIL: %s\n", what);
    ++g_failures;
}

rule::Action action(int32_t state, int32_t id, int32_t a, int32_t b) { return {{state, id, a, b}}; }

void testNothingBeforeAnOwnerRecord() {
    rule::Sync sync;
    check(!sync.due(action(1, 2, 1, 0), 10000), "no owner record: nothing to request");
}

void testMatchingRecordIsLeftAlone() {
    rule::Sync sync;
    sync.observeOwner(action(1, 2, 1, 0), 0);
    check(!sync.due(action(1, 2, 1, 0), 10000), "equal records: nothing to request");
}

void testRequestAfterSettlingAndMismatch() {
    rule::Sync sync;
    sync.observeOwner(action(1, 5, 2, 1), 0);
    check(!sync.due(action(1, 2, 1, 0), 0), "a fresh mismatch is not due");
    check(!sync.due(action(1, 2, 1, 0), rule::kMismatchMs - 1), "a short mismatch is not due");
    check(sync.due(action(1, 2, 1, 0), rule::kMismatchMs), "a persistent mismatch against a settled owner is due");
}

void testOwnerFlipsAreNotFollowedAtOnce() {
    rule::Sync sync;
    sync.observeOwner(action(1, 5, 2, 1), 0);
    check(!sync.due(action(1, 2, 1, 0), 0), "mismatch starts");
    sync.observeOwner(action(1, 8, 0, 1), 250);
    check(!sync.due(action(1, 2, 1, 0), 250), "the owner changed just now: wait for it to settle");
    check(sync.due(action(1, 2, 1, 0), 250 + rule::kOwnerStableMs), "settled: request");
}

void testCooldown() {
    rule::Sync sync;
    sync.observeOwner(action(1, 5, 2, 1), 0);
    check(!sync.due(action(1, 2, 1, 0), 0), "mismatch starts");
    check(sync.due(action(1, 2, 1, 0), 1000), "first request");
    check(!sync.due(action(1, 2, 1, 0), 1000 + rule::kCooldownMs - 1), "no second request inside the cooldown");
    check(sync.due(action(1, 2, 1, 0), 1000 + rule::kCooldownMs), "another one after it");
}

void testMatchClearsTheMismatch() {
    rule::Sync sync;
    sync.observeOwner(action(1, 5, 2, 1), 0);
    check(!sync.due(action(1, 2, 1, 0), 0), "mismatch starts");
    check(!sync.due(action(1, 5, 2, 1), 150), "the puppet caught up");
    check(!sync.due(action(1, 2, 1, 0), 300), "a new mismatch starts over");
    check(sync.due(action(1, 2, 1, 0), 300 + rule::kMismatchMs), "and is due after its own wait");
}

}  // namespace

int main() {
    testNothingBeforeAnOwnerRecord();
    testMatchingRecordIsLeftAlone();
    testRequestAfterSettlingAndMismatch();
    testOwnerFlipsAreNotFollowedAtOnce();
    testCooldown();
    testMatchClearsTheMismatch();
    if (g_failures == 0) std::printf("enemy_action_rule_test: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
