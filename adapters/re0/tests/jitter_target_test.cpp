#include <cstdio>

#include "../src/jitter_target.h"

namespace {

int g_failures = 0;

void check(bool ok, const char* what) {
    if (ok) return;
    ++g_failures;
    std::printf("FAIL: %s\n", what);
}

}  // namespace

int main() {
    JitterTarget jitter;
    check(jitter.target() == JitterTarget::kInitial, "starts at the initial target");
    jitter.onUnderrun();
    check(jitter.target() == JitterTarget::kInitial + 1, "an underrun adds a frame");
    for (int i = 0; i < 20; ++i) jitter.onUnderrun();
    check(jitter.target() == JitterTarget::kMax, "capped at the maximum");
    check(jitter.maxBehind() == JitterTarget::kMax + JitterTarget::kSlack, "skip threshold follows the target");

    for (uint32_t i = 0; i < JitterTarget::kCalmFrames - 1; ++i) jitter.onFrameConsumed();
    check(jitter.target() == JitterTarget::kMax, "no shrink before a full calm stretch");
    jitter.onFrameConsumed();
    check(jitter.target() == JitterTarget::kMax - 1, "a calm stretch removes a frame");

    jitter.onUnderrun();
    for (uint32_t i = 0; i < JitterTarget::kCalmFrames - 1; ++i) jitter.onFrameConsumed();
    check(jitter.target() == JitterTarget::kMax, "an underrun restarts the calm count");

    for (uint32_t i = 0; i < JitterTarget::kCalmFrames * 20; ++i) jitter.onFrameConsumed();
    check(jitter.target() == JitterTarget::kMin, "floored at the minimum");

    if (g_failures == 0) std::printf("jitter_target_test: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
