// Checks a banner's fade: in over 0.5 s, held, out over 0.6 s, and a life too short for both (no game). Exit 0 when every
// check passes.
#include <cmath>
#include <cstdio>

#include "../src/toast_rule.h"

namespace {

using namespace toast_rule;

int g_failures = 0;

void check(bool ok, const char* what) {
    if (ok) return;
    std::printf("FAIL: %s\n", what);
    ++g_failures;
}

bool near(float a, float b) { return std::fabs(a - b) < 1e-4f; }

void testFade() {
    constexpr float total = 3.0f;
    check(near(alpha(0.0f, total), 0.0f), "a banner starts invisible");
    check(near(alpha(0.25f, total), 0.5f), "and is half there after half the fade in");
    check(near(alpha(kFadeInSeconds, total), 1.0f) && near(alpha(1.5f, total), 1.0f), "it holds at full");
    check(near(alpha(total - kFadeOutSeconds * 0.5f, total), 0.5f), "it is half gone after half the fade out");
    check(near(alpha(total, total), 0.0f) && near(alpha(total + 1.0f, total), 0.0f), "and gone at its end");
    check(near(alpha(-1.0f, total), 0.0f), "never below zero");
}

void testShortLife() {
    constexpr float total = 0.8f;
    check(alpha(0.4f, total) < 1.0f, "a life shorter than both ramps never reaches full");
    check(near(alpha(0.3f, total), std::min(0.3f / kFadeInSeconds, 0.5f / kFadeOutSeconds)), "and takes the lower ramp");
}

}  // namespace

int main() {
    testFade();
    testShortLife();
    if (g_failures == 0) std::printf("toast_rule_test: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
