#include <cstdio>

#include "../src/flag_family.h"

namespace {

int g_failures = 0;

void check(bool ok, const char* what) {
    if (ok) return;
    ++g_failures;
    std::printf("FAIL: %s\n", what);
}

}  // namespace

int main() {
    using flag_family::Family;
    using flag_family::classify;

    check(classify(0x000) == Family::Story, "first story flag");
    check(classify(0x6f) == Family::Story, "lift flag 0x6f is story");
    check(classify(0x311) == Family::Story, "last story bit");
    check(classify(0x312) == Family::Enemy, "enemy base");
    check(classify(0x3d9) == Family::Enemy, "enemy bit past the last named one stays enemy");
    check(classify(0x411) == Family::Enemy, "last enemy bit");
    check(classify(0x412) == Family::Effect, "effect base");
    check(classify(0x4b6) == Family::Effect, "effect bit seen set in a save");
    check(classify(0x611) == Family::Effect, "last effect bit");
    check(classify(0x612) == Family::Act, "act base");
    check(classify(0x6f6) == Family::Act, "act bit seen set in a save");
    check(classify(0x711) == Family::Act, "last act bit");
    check(classify(0x712) == Family::Display, "display base");
    check(classify(0x78c) == Family::Display, "display bit seen set in a save");
    check(classify(0x811) == Family::Display, "last display bit");
    check(classify(0x812) == Family::Bgm, "bgm base");
    check(classify(0x8df) == Family::Bgm, "last bit of the bitset");
    check(classify(0x8e0) == Family::Unmapped, "past the bitset");

    if (g_failures == 0) std::printf("flag_family_test: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
