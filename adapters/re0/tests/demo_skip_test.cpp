// Checks the pure rules and the wire format of the shared cutscene skip (no game). Exit 0 when every check passes.
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "../src/demo_skip_rule.h"

namespace {

namespace rule = demo_skip_rule;
namespace wire = demo_skip_wire;

constexpr uint16_t kDemo = 0x14;
constexpr uint16_t kOtherDemo = 0x15;
constexpr uint16_t kSerial = 7;
constexpr uint16_t kOtherSerial = 8;
constexpr uint32_t kNow = 1000;

int g_failures = 0;

void check(bool ok, const char* what) {
    if (ok) return;
    std::printf("FAIL: %s\n", what);
    ++g_failures;
}

rule::Running running(bool leads, bool serialMine = true) { return {kDemo, kSerial, serialMine, leads}; }

wire::DemoSkip skipFor(bool serialIsReceivers = true) {
    return {kDemo, kSerial, serialIsReceivers ? event_sync::kSerialIsReceivers : uint8_t{0}, 0};
}

void testWire() {
    check(sizeof(wire::DemoSkip) == 6, "a skip is 6 bytes");
    check(wire::kMsgDemoSkipRequest == 0x0153 && wire::kMsgDemoSkip == 0x0154, "message ids of the state block");
    const wire::DemoSkip skip = skipFor();
    uint8_t bytes[sizeof(skip)];
    std::memcpy(bytes, &skip, sizeof(skip));
    const auto back = wire::decode(bytes);
    check(back && back->demoId == kDemo && back->serial == kSerial, "a skip round-trips");
    check(!wire::decode(std::span<const uint8_t>(bytes, sizeof(bytes) - 1)), "a short payload is refused");
    wire::DemoSkip bad = skip;
    bad.serial = 0;
    std::memcpy(bytes, &bad, sizeof(bad));
    check(!wire::decode(bytes), "serial 0 is refused");
    bad = skip;
    bad.demoId = wire::kDemoIdLimit;
    std::memcpy(bytes, &bad, sizeof(bad));
    check(!wire::decode(bytes), "a demo id past the limit is refused");
    bad = skip;
    bad.flags = 2;
    std::memcpy(bytes, &bad, sizeof(bad));
    check(!wire::decode(bytes), "unknown flags are refused");
    check(wire::flagsFor(true) == 0 && wire::flagsFor(false) == event_sync::kSerialIsReceivers, "flags say whose serial");
}

void testMatches() {
    check(rule::matches(running(true, true), skipFor(false)) == false, "a serial the sender allocated is not the receiver's");
    check(rule::matches(running(true, true), skipFor(true)), "a skip names the demo and thread");
    check(rule::matches(running(false, false), skipFor(false)), "the other side's serial");
    wire::DemoSkip otherDemo = skipFor();
    otherDemo.demoId = kOtherDemo;
    check(!rule::matches(running(true), otherDemo), "another demo does not match");
    wire::DemoSkip otherThread = skipFor();
    otherThread.serial = kOtherSerial;
    check(!rule::matches(running(true), otherThread), "another thread does not match");
}

void testPress() {
    check(rule::onPress(false, true) == rule::Press::Ignore, "no demo, no skip");
    check(rule::onPress(true, true) == rule::Press::SendSkip, "the leader tells the follower");
    check(rule::onPress(true, false) == rule::Press::SendRequest, "the follower asks the leader");
}

void testInput() {
    constexpr uint32_t kPad = 8, kKeys = 0x8050000;
    constexpr uint8_t kMouse = 2;
    check(!rule::skipInput(0, 0, 0, kPad, kKeys, kMouse), "no input");
    check(rule::skipInput(kPad, 0, 0, kPad, kKeys, kMouse), "the pad bit");
    check(rule::skipInput(0, 0x10000, 0, kPad, kKeys, kMouse), "a key");
    check(rule::skipInput(0, 0, 3, kPad, kKeys, kMouse), "the mouse button");
    check(!rule::skipInput(0x7, 0x1, 0x1, kPad, kKeys, kMouse), "other bits do not skip");
}

void testTracker() {
    rule::Tracker tracker;
    check(!tracker.running() && !tracker.take(kNow), "nothing runs, nothing to take");
    tracker.start(running(false, false));
    tracker.add(skipFor(false), true, kNow);
    check(tracker.take(kNow + 5), "the leader's skip applies to the follower's running demo");
    check(!tracker.take(kNow + 6), "a skip applies once");

    tracker.add(skipFor(false), false, kNow);
    check(!tracker.take(kNow), "a request is not for a follower");

    tracker.end();
    tracker.add(skipFor(false), true, kNow);
    check(!tracker.take(kNow), "no demo here, no skip");
    tracker.start(running(false, false));
    check(tracker.take(kNow + 1), "a skip that arrived before the demo waits for it");

    tracker.start(running(true, true));
    tracker.add(skipFor(true), false, kNow);
    check(tracker.take(kNow + 1), "the leader takes the follower's request");

    tracker.add(skipFor(true), false, kNow);
    check(!tracker.take(kNow + rule::kPendingMs), "a lapsed skip is dropped");
    tracker.start(running(true, true));
    check(!tracker.take(kNow + rule::kPendingMs + 1), "and stays dropped for a later demo");

    tracker.start({kOtherDemo, kSerial, true, true});
    tracker.add(skipFor(true), false, kNow);
    check(!tracker.take(kNow), "a skip for another demo is not taken");
    check(rule::ageMs(0xfffffff0u, 0x10u) == 0x20, "age is wrap-safe");
}

}  // namespace

int main() {
    testWire();
    testMatches();
    testPress();
    testInput();
    testTracker();
    if (g_failures == 0) std::printf("demo_skip_test: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
