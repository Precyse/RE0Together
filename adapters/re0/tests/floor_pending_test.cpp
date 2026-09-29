// Checks the per-room pending floor queue: put/take coalescing, the per-room cap and drop reporting, ordering and
// room isolation. Exit 0 when every check passes.
#include <cstdio>

#include "../src/floor_pending.h"

namespace {

using floor_pending::Event;
using floor_pending::Queue;
using floor_pending::Vec3;

constexpr uint16_t kRoomA = 0x0102;
constexpr uint16_t kRoomB = 0x0103;
constexpr uint32_t kHerb = 0x2d;
constexpr uint32_t kKnife = 2;
constexpr Vec3 kOrigin{0.0f, 0.0f, 0.0f};
int g_failures = 0;

void check(bool ok, const char* what) {
    if (ok) return;
    std::printf("FAIL: %s\n", what);
    ++g_failures;
}

Event put(uint32_t itemId, Vec3 pos = kOrigin) { return {false, itemId, 1, pos, kOrigin}; }
Event take(uint32_t itemId, Vec3 pos = kOrigin) { return {true, itemId, 0, pos, kOrigin}; }

void testTakeCancelsMatchingPut() {
    Queue queue;
    queue.add(kRoomA, put(kHerb));
    const auto result = queue.add(kRoomA, take(kHerb, {floor_pending::kMatchDistance - 1.0f, 0.0f, 0.0f}));
    check(result.cancelledPut && !result.droppedOldest, "matching take cancels the put");
    check(queue.total() == 0, "both events are removed");
}

void testTakeThatDoesNotMatchIsStored() {
    Queue queue;
    queue.add(kRoomA, put(kHerb));
    check(!queue.add(kRoomA, take(kKnife)).cancelledPut, "different item id does not cancel");
    check(!queue.add(kRoomA, take(kHerb, {floor_pending::kMatchDistance + 1.0f, 0.0f, 0.0f})).cancelledPut,
          "take beyond the match distance does not cancel");
    check(!queue.add(kRoomB, take(kHerb)).cancelledPut, "take in another room does not cancel");
    check(queue.total() == 4, "unmatched takes are stored");
}

void testTakeCancelsNearestPut() {
    Queue queue;
    queue.add(kRoomA, put(kHerb, {40.0f, 0.0f, 0.0f}));
    queue.add(kRoomA, put(kHerb, {10.0f, 0.0f, 0.0f}));
    queue.add(kRoomA, take(kHerb));
    const std::vector<Event> left = queue.take(kRoomA);
    check(left.size() == 1 && left[0].pos[0] == 40.0f, "the nearest put is cancelled");
}

void testCapDropsOldest() {
    Queue queue;
    for (uint32_t i = 0; i < floor_pending::kMaxPendingPerRoom; ++i) {
        check(!queue.add(kRoomA, put(kKnife + i, {1000.0f * static_cast<float>(i), 0.0f, 0.0f})).droppedOldest,
              "no drop below the cap");
    }
    check(queue.add(kRoomA, put(kHerb)).droppedOldest, "drop reported at the cap");
    const std::vector<Event> events = queue.take(kRoomA);
    check(events.size() == floor_pending::kMaxPendingPerRoom, "room stays at the cap");
    check(events.front().itemId == kKnife + 1 && events.back().itemId == kHerb, "the oldest event was dropped");
}

void testTakeReturnsOrderAndClears() {
    Queue queue;
    queue.add(kRoomA, put(kKnife));
    queue.add(kRoomB, put(kHerb));
    queue.add(kRoomA, take(kHerb));
    const std::vector<Event> events = queue.take(kRoomA);
    check(events.size() == 2 && !events[0].isTake && events[1].isTake, "events come back in arrival order");
    check(queue.take(kRoomA).empty(), "the room is cleared");
    check(queue.total() == 1, "other rooms are untouched");
}

}  // namespace

int main() {
    testTakeCancelsMatchingPut();
    testTakeThatDoesNotMatchIsStored();
    testTakeCancelsNearestPut();
    testCapDropsOldest();
    testTakeReturnsOrderAndClears();
    std::printf(g_failures ? "%d failure(s)\n" : "all checks passed\n", g_failures);
    return g_failures ? 1 : 0;
}
