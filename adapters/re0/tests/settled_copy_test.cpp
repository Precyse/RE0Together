// Checks the inventory/floor change tracking: settle delay, no send while unchanged, periodic resend, adopt.
// Exit 0 when every check passes.
#include <cstdio>

#include "../src/settled_copy.h"

namespace {

using Copy = SettledCopy<4>;

constexpr uint64_t kResendMs = 5000;
int g_failures = 0;

void check(bool ok, const char* what) {
    if (ok) return;
    std::printf("FAIL: %s\n", what);
    ++g_failures;
}

Copy::Bytes bytes(uint8_t first) { return {first, 0, 0, 0}; }

void testFirstObservationSendsAfterSettle() {
    Copy copy;
    check(!copy.observe(bytes(1), 0), "first observation is not a change");
    check(!copy.due(kSettleFrames - 1, 0, kResendMs), "not due before settling");
    check(copy.due(kSettleFrames, 0, kResendMs), "due once settled");
    copy.markSent(0);
    check(!copy.due(kSettleFrames + 1, 1, kResendMs), "not due after sending");
}

void testChangeRestartsSettle() {
    Copy copy;
    copy.observe(bytes(1), 0);
    copy.markSent(0);
    check(copy.observe(bytes(2), 100), "change detected");
    check(!copy.due(100 + kSettleFrames - 1, 1, kResendMs), "not due while settling");
    check(copy.observe(bytes(3), 105), "second change detected");
    check(!copy.due(100 + kSettleFrames, 1, kResendMs), "settle restarted by the second change");
    check(copy.due(105 + kSettleFrames, 1, kResendMs), "due after the restarted settle");
}

void testChangeBackToSentIsNotDue() {
    Copy copy;
    copy.observe(bytes(1), 0);
    copy.markSent(0);
    copy.observe(bytes(2), 50);
    copy.observe(bytes(1), 51);
    check(!copy.due(200, 1, kResendMs), "reverting to the sent copy needs no send");
}

void testResend() {
    Copy copy;
    copy.observe(bytes(1), 0);
    copy.markSent(1000);
    check(!copy.due(500, 1000 + kResendMs - 1, kResendMs), "no resend before the interval");
    check(copy.due(500, 1000 + kResendMs, kResendMs), "resend after the interval");
    check(!copy.due(500, 1000 + kResendMs, 0), "resend disabled with interval 0");
}

void testAdoptAndReset() {
    Copy copy;
    copy.adopt(bytes(7), 0);
    check(copy.initialised(), "adopt initialises");
    check(!copy.observe(bytes(7), 5), "adopted contents are not a change");
    check(!copy.due(100, 1, 0), "adopted contents are not due");
    check(copy.observe(bytes(8), 6), "later change detected");
    copy.reset();
    check(!copy.initialised(), "reset forgets the state");
}

}  // namespace

int main() {
    testFirstObservationSendsAfterSettle();
    testChangeRestartsSettle();
    testChangeBackToSentIsNotDue();
    testResend();
    testAdoptAndReset();
    if (g_failures == 0) std::printf("settled_copy_test: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
