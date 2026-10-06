// Checks the enemy AI replication rule (no game). Exit 0 when every check passes.
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
    check(!sync.due(action(1, 2, 1, 0), 10000), "no owner record: nothing to apply");
}

void testMatchingRecordIsLeftAlone() {
    rule::Sync sync;
    sync.observeOwner(action(1, 2, 1, 0));
    check(!sync.due(action(1, 2, 1, 0), 10000), "equal records: nothing to apply");
}

void testAppliedAtOnce() {
    rule::Sync sync;
    sync.observeOwner(action(1, 5, 2, 1));
    check(sync.due(action(1, 2, 1, 0), 0), "a different owner record is applied with no wait");
}

void testCooldown() {
    rule::Sync sync;
    sync.observeOwner(action(1, 5, 2, 1));
    check(sync.due(action(1, 2, 1, 0), 1000), "first application");
    check(!sync.due(action(1, 2, 1, 0), 1000 + rule::kCooldownMs - 1), "not again inside the cooldown");
    check(sync.due(action(1, 2, 1, 0), 1000 + rule::kCooldownMs), "again after it");
}

void testOwnerChangeIsFollowed() {
    rule::Sync sync;
    sync.observeOwner(action(1, 5, 2, 1));
    check(sync.due(action(1, 2, 1, 0), 0), "first record applied");
    sync.observeOwner(action(1, 8, 0, 1));
    check(!sync.due(action(1, 8, 0, 1), 500), "a puppet that already matches is left alone");
    check(sync.due(action(1, 5, 2, 1), 500), "a new owner record is applied once the cooldown allows");
}

}  // namespace

int main() {
    testNothingBeforeAnOwnerRecord();
    testMatchingRecordIsLeftAlone();
    testAppliedAtOnce();
    testCooldown();
    testOwnerChangeIsFollowed();
    if (g_failures == 0) std::printf("enemy_action_rule_test: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
