// Checks the load serial (no game): it advances when the loaded room record or scene changes, and is never 0.
// Exit 0 when every check passes.
#include <cstdio>

#include "../src/load_serial.h"

namespace {

using load_serial::Tracker;

constexpr uintptr_t kRecordA = 0x1000;
constexpr uintptr_t kRecordB = 0x2000;
constexpr uintptr_t kRecordStep = 0x10;
constexpr uint16_t kSceneA = 0x10;
constexpr uint16_t kSceneB = 0x21;
int g_failures = 0;

void check(bool ok, const char* what) {
    if (ok) return;
    std::printf("FAIL: %s\n", what);
    ++g_failures;
}

void testFirstLoad() {
    Tracker tracker;
    check(tracker.value() == load_serial::kFirstSerial, "the serial starts at the first serial");
    check(!tracker.observe(0, kSceneA), "nothing loaded is not a change");
    check(tracker.observe(kRecordA, kSceneA), "the first loaded record advances the serial");
    check(tracker.value() == load_serial::kFirstSerial + 1, "by one");
}

void testChanges() {
    Tracker tracker;
    tracker.observe(kRecordA, kSceneA);
    const uint16_t first = tracker.value();
    check(!tracker.observe(kRecordA, kSceneA), "the same record and scene is not a change");
    check(tracker.value() == first, "so the serial stays");
    check(tracker.observe(kRecordB, kSceneB), "a different record advances the serial");
    check(tracker.value() == first + 1, "by one");
    check(tracker.observe(kRecordB, kSceneA), "a different scene in the same record advances it");
    check(tracker.observe(kRecordA, kSceneA), "going back advances it again");
    check(tracker.value() == first + 3, "each change counts once");
}

void testLoadInBetween() {
    Tracker tracker;
    tracker.observe(kRecordA, kSceneA);
    const uint16_t before = tracker.value();
    check(!tracker.observe(0, kSceneA), "a load in progress is not a change by itself");
    check(tracker.value() == before, "the serial stays while loading");
    check(tracker.observe(kRecordB, kSceneB), "the new record after a load advances once");
    check(tracker.value() == before + 1, "by one");
    tracker.observe(0, kSceneB);
    check(tracker.observe(kRecordB, kSceneB), "the same record again after a load is a new load");
}

void testWrap() {
    Tracker tracker;
    uintptr_t record = kRecordA;
    bool neverZero = true;
    while (tracker.value() != UINT16_MAX) {
        tracker.observe(record, kSceneA);
        record += kRecordStep;
        neverZero = neverZero && tracker.value() != 0;
    }
    check(neverZero, "the serial is never 0 on the way up");
    check(tracker.observe(record, kSceneA), "one more change at the top");
    check(tracker.value() == load_serial::kFirstSerial, "wraps past 0 to the first serial");
}

}  // namespace

int main() {
    testFirstLoad();
    testChanges();
    testLoadInBetween();
    testWrap();
    if (g_failures == 0) std::printf("load_serial_test: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
