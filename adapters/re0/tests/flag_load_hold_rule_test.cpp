// Checks the flag tracker's hold across a local load (no game): it stands still from the load request until the loaded
// room's record is in, and never longer than the cap. Exit 0 when every check passes.
#include <cstdio>

#include "../src/flag_load_hold_rule.h"

namespace {

using flag_load_hold_rule::kMaxHoldFrames;
using flag_load_hold_rule::LoadHold;

constexpr uint16_t kSerial = 7;
constexpr uint32_t kFrame = 600;
int g_failures = 0;

void check(bool ok, const char* what) {
    if (ok) return;
    std::printf("FAIL: %s\n", what);
    ++g_failures;
}

void testIdleHoldsNothing() {
    LoadHold hold;
    check(!hold.holding(kSerial, kFrame), "without a load request the tracker runs");
}

void testHoldsUntilTheRoomIsIn() {
    LoadHold hold;
    hold.begin(kSerial, kFrame);
    check(hold.holding(kSerial, kFrame + 6), "the save's flags are not a story change while the load runs");
    check(hold.holding(kSerial, kFrame + 60), "still the old room record, still holding");
    check(!hold.holding(kSerial + 1, kFrame + 90), "the loaded room's record ends the hold");
    check(!hold.holding(kSerial + 1, kFrame + 96), "and it stays ended");
}

void testCapEndsAHoldThatNeverSeesTheRecord() {
    LoadHold hold;
    hold.begin(kSerial, kFrame);
    check(hold.holding(kSerial, kFrame + kMaxHoldFrames - 1), "held just before the cap");
    check(!hold.holding(kSerial, kFrame + kMaxHoldFrames), "the cap ends it");
    check(!hold.holding(kSerial, kFrame + kMaxHoldFrames + 6), "and it stays ended");
}

void testCapAcrossFrameWrap() {
    LoadHold hold;
    const uint32_t nearWrap = 0xfffffff0u;
    hold.begin(kSerial, nearWrap);
    check(hold.holding(kSerial, nearWrap + 100), "held across the frame counter wrap");
    check(!hold.holding(kSerial, nearWrap + kMaxHoldFrames), "the cap holds across the wrap");
}

void testNewLoadRestartsAndCancelEnds() {
    LoadHold hold;
    hold.begin(kSerial, kFrame);
    hold.holding(kSerial + 1, kFrame + 30);
    hold.begin(kSerial + 1, kFrame + 60);
    check(hold.holding(kSerial + 1, kFrame + 66), "a second load holds again from its own request");
    hold.cancel();
    check(!hold.holding(kSerial + 1, kFrame + 72), "a dropped link ends the hold");
}

}  // namespace

int main() {
    testIdleHoldsNothing();
    testHoldsUntilTheRoomIsIn();
    testCapEndsAHoldThatNeverSeesTheRecord();
    testCapAcrossFrameWrap();
    testNewLoadRestartsAndCancelEnds();
    if (g_failures == 0) std::printf("flag_load_hold_rule_test: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
