// Checks the enemy class policy table (no game): every one of the 38 vtables is classified, the Legacy and Native sets
// are exactly the ones STATE_ENEMIES 6.4 names, and the wire index is the position in game::kEnemyVtables. Exit 0 when
// every check passes.
#include <cstdio>
#include <cstring>
#include <set>
#include <string>

#include "../src/enemy_policy.h"

namespace {

namespace policy = enemy_policy;

int g_failures = 0;

void check(bool ok, const char* what) {
    if (ok) return;
    std::printf("FAIL: %s\n", what);
    ++g_failures;
}

size_t countOf(policy::Policy wanted) {
    size_t count = 0;
    for (const policy::EnemyClass& enemyClass : policy::kClasses) count += enemyClass.policy == wanted ? 1 : 0;
    return count;
}

std::set<std::string> namesOf(policy::Policy wanted) {
    std::set<std::string> names;
    for (const policy::EnemyClass& enemyClass : policy::kClasses) {
        if (enemyClass.policy == wanted) names.insert(enemyClass.name);
    }
    return names;
}

void testEveryClassIsClassified() {
    check(policy::kClassCount == 38, "38 enemy classes");
    check(countOf(policy::Policy::Observer) == 31, "31 Observer classes");
    check(countOf(policy::Policy::Legacy) == 4, "4 Legacy classes");
    check(countOf(policy::Policy::Native) == 3, "3 Native classes");
    check(countOf(policy::Policy::Observer) + countOf(policy::Policy::Legacy) + countOf(policy::Policy::Native) == policy::kClassCount,
          "every class has exactly one policy");
}

void testLegacyAndNativeSetsAreExact() {
    check(namesOf(policy::Policy::Legacy) == std::set<std::string>({"uEnemy3a", "uEnemy3e", "uEnemy43", "uEnemy47"}),
          "Legacy = Centurion 3a / 3e, 43, 47");
    check(namesOf(policy::Policy::Native) == std::set<std::string>({"uEnemy3bRebecca", "uEnemy44", "uEnemy45"}),
          "Native = 3bRebecca, 44, 45");
}

void testObserverSetHoldsTheNamedClasses() {
    const std::set<std::string> observers = namesOf(policy::Policy::Observer);
    for (const char* name : {"uEnemy10", "uEnemy11", "uEnemy12", "uEnemy14", "uEnemy15", "uEnemy16", "uEnemy17", "uEnemy19",
                             "uEnemy1a", "uEnemy50", "uEnemy51", "uEnemy5a", "uEnemy5b", "uEnemy6a", "uEnemy6b"}) {
        check(observers.count(name) == 1, "the 15 base family vtables observe");
    }
    for (const char* name : {"uEnemy2a", "uEnemy2b", "uEnemy2c", "uEnemy2f", "uEnemy30", "uEnemy31", "uEnemy34", "uEnemy35",
                             "uEnemy37", "uEnemy38", "uEnemy39", "uEnemy3c", "uEnemy40", "uEnemy41", "uEnemy42", "uEnemy46"}) {
        check(observers.count(name) == 1, "the other named Observer classes observe");
    }
}

void testWireIndexIsTheGameOrder() {
    for (size_t i = 0; i < policy::kClassCount; ++i) {
        check(policy::classIndexOf(game::kEnemyVtables[i]) == static_cast<int>(i), "class index = position in kEnemyVtables");
        check(policy::policyOf(game::kEnemyVtables[i]) == policy::kClasses[i].policy, "policyOf by vtable");
    }
    std::set<std::string> names;
    for (const policy::EnemyClass& enemyClass : policy::kClasses) names.insert(enemyClass.name);
    check(names.size() == policy::kClassCount, "class names are unique");
}

void testOnlyRebeccaLacksASetAction() {
    std::set<std::string> without;
    for (const policy::EnemyClass& enemyClass : policy::kClasses) {
        if (!enemyClass.hasSetAction) without.insert(enemyClass.name);
    }
    check(without == std::set<std::string>({"uEnemy3bRebecca"}), "only uEnemy3bRebecca has no setAction in slot 63");
    check(!policy::hasSetAction(0xcc1618), "hasSetAction by vtable: 3bRebecca");
    check(policy::hasSetAction(0xcc13d8) && policy::hasSetAction(0xcbdcd8), "hasSetAction by vtable: the other classes");
    check(policy::hasSetAction(0), "an unknown vtable keeps the call it always had");
}

void testUnknownVtableIsNeverReplicated() {
    check(policy::classIndexOf(0) == policy::kNoClass, "no class for vtable 0");
    check(policy::classIndexOf(game::kControlledThinkVtable) == policy::kNoClass, "no class for a think vtable");
    check(policy::policyOf(0) == policy::Policy::Native, "an unknown vtable is Native");
    check(std::strcmp(policy::nameOf(policy::kClassCount), "?") == 0, "name past the table");
    check(std::strcmp(policy::nameOf(0), "uEnemy10") == 0, "name of class 0");
}

}  // namespace

int main() {
    testEveryClassIsClassified();
    testLegacyAndNativeSetsAreExact();
    testObserverSetHoldsTheNamedClasses();
    testWireIndexIsTheGameOrder();
    testOnlyRebeccaLacksASetAction();
    testUnknownVtableIsNeverReplicated();
    if (g_failures == 0) std::printf("enemy_policy_test: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
