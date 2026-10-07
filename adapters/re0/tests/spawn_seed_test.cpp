// Checks the enemy creation seed (no game). Exit 0 when every check passes.
#include <cstdio>

#include "../src/spawn_seed.h"

namespace {

int g_failures = 0;

void check(bool ok, const char* what) {
    if (ok) return;
    std::printf("FAIL: %s\n", what);
    ++g_failures;
}

// The game's generator (0x684d90): the next value from a state.
uint32_t next(spawn_seed::State& s) {
    uint32_t t = s[0] ^ (s[0] << 15);
    s[0] = s[1];
    s[1] = s[2];
    s[2] = s[3];
    s[3] = ((((s[3] >> 17) ^ t) >> 4) ^ s[3]) ^ t;
    return s[3];
}

void testSameRecordSameState() {
    check(spawn_seed::stateFor(0x25, 3, 0x11, 0x40) == spawn_seed::stateFor(0x25, 3, 0x11, 0x40),
          "both machines build the same state from the same record");
}

void testRecordsDiffer() {
    const spawn_seed::State base = spawn_seed::stateFor(0x25, 3, 0x11, 0x40);
    check(base != spawn_seed::stateFor(0x26, 3, 0x11, 0x40), "another scene");
    check(base != spawn_seed::stateFor(0x25, 4, 0x11, 0x40), "another record");
    check(base != spawn_seed::stateFor(0x25, 3, 0x12, 0x40), "another kind");
    check(base != spawn_seed::stateFor(0x25, 3, 0x11, 0x41), "another spawn id");
}

void testNeverAllZero() {
    const spawn_seed::State zero = spawn_seed::stateFor(0, 0, 0, 0);
    check(zero[0] != 0, "an all-zero input still gives a working state");
}

void testVariantsSpread() {
    // The base family's variant is rand % 3: across records every variant must come up.
    constexpr int kVariants = 3;
    int seen[kVariants] = {};
    for (uint16_t index = 0; index < 60; ++index) {
        spawn_seed::State state = spawn_seed::stateFor(0x25, index, 0x11, index);
        ++seen[next(state) % kVariants];
    }
    check(seen[0] > 0 && seen[1] > 0 && seen[2] > 0, "every variant comes up across records");
}

}  // namespace

int main() {
    testSameRecordSameState();
    testRecordsDiffer();
    testNeverAllZero();
    testVariantsSpread();
    if (g_failures == 0) std::printf("spawn_seed_test: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
