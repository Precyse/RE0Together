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
    check(classify(0x1c0) == Family::Story, "last story flag");
    check(classify(0x1c1) == Family::Unmapped, "gap after story");
    check(classify(0x312) == Family::Enemy, "enemy base");
    check(classify(0x3d8) == Family::Enemy, "last enemy flag");
    check(classify(0x3d9) == Family::Unmapped, "gap after enemy");
    check(classify(0x412) == Family::Effect, "effect base");
    check(classify(0x4af) == Family::Effect, "last effect flag");
    check(classify(0x4b0) == Family::Unmapped, "gap after effect");
    check(classify(0x612) == Family::Act, "act base");
    check(classify(0x6be) == Family::Act, "last act flag");
    check(classify(0x712) == Family::Display, "display base");
    check(classify(0x784) == Family::Display, "last display flag");
    check(classify(0x812) == Family::Bgm, "bgm base");
    check(classify(0x84c) == Family::Bgm, "last bgm flag");
    check(classify(0x84d) == Family::Unmapped, "gap after bgm");

    if (g_failures == 0) std::printf("flag_family_test: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
