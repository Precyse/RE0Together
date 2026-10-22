// Checks the timed queue (no game): sequence order, due by the peer's time, once only, duplicates, stale items and the
// size cap. Exit 0 when every check passes.
#include <cstdio>
#include <vector>

#include "../src/timed_queue.h"

namespace {

namespace tq = timed_queue;

struct Item {
    uint32_t senderMs;
    uint32_t seq;
};

constexpr uint32_t kShown = 5000;
int g_failures = 0;

void check(bool ok, const char* what) {
    if (ok) return;
    std::printf("FAIL: %s\n", what);
    ++g_failures;
}

void testDueInOrder() {
    tq::TimedQueue<Item> queue;
    queue.push({4900, 2});
    queue.push({4800, 1});
    queue.push({kShown + 50, 3});
    std::vector<Item> due;
    check(queue.takeDue(kShown, due) == 0 && due.size() == 2, "items at or before the shown time are due");
    check(due[0].seq == 1 && due[1].seq == 2, "they come in sequence order");
    due.clear();
    check(queue.takeDue(kShown, due) == 0 && due.empty(), "an item is handed out once");
    queue.takeDue(kShown + 50, due);
    check(due.size() == 1 && due[0].seq == 3, "a later item comes when its time does");
}

void testDuplicates() {
    tq::TimedQueue<Item> queue;
    queue.push({4900, 1});
    queue.push({4900, 1});
    std::vector<Item> due;
    queue.takeDue(kShown, due);
    check(due.size() == 1, "a duplicate waiting in the queue is dropped");
    queue.push({4900, 1});
    queue.takeDue(kShown, due);
    check(due.size() == 1 && queue.size() == 0, "a handed-out sequence is dropped");
    queue.push({4950, 2});
    queue.takeDue(kShown, due);
    check(due.size() == 2, "the next sequence still comes");
}

void testStale() {
    tq::TimedQueue<Item> queue;
    queue.push({kShown - tq::kStaleMs - 1, 1});
    queue.push({kShown - 10, 2});
    std::vector<Item> due;
    const uint32_t stale = queue.takeDue(kShown, due);
    check(stale == 1 && due.size() == 1 && due[0].seq == 2, "an item far behind the shown time is dropped and counted");
    queue.push({kShown - tq::kStaleMs - 1, 3});
    due.clear();
    check(queue.takeDue(kShown, due) == 1, "a stale item is still marked handed out");
}

void testCap() {
    tq::TimedQueue<Item> queue;
    for (uint32_t i = 1; i <= tq::kMaxItems + 5; ++i) queue.push({kShown, i});
    check(queue.size() == tq::kMaxItems, "the queue holds at most kMaxItems");
    std::vector<Item> due;
    queue.takeDue(kShown, due);
    check(due.front().seq == 6, "the oldest sequences are the ones that fall off");
}

void testClear() {
    tq::TimedQueue<Item> queue;
    queue.push({4900, 7});
    std::vector<Item> due;
    queue.takeDue(kShown, due);
    queue.clear();
    queue.push({4900, 1});
    due.clear();
    queue.takeDue(kShown, due);
    check(due.size() == 1, "numbering starts over after a clear");
}

void testSequenceWrap() {
    tq::TimedQueue<Item> queue;
    queue.push({4900, 0xffffffffu});
    queue.push({4910, 0});
    std::vector<Item> due;
    queue.takeDue(kShown, due);
    check(due.size() == 2 && due[0].seq == 0xffffffffu && due[1].seq == 0, "sequences are ordered across the 32-bit wrap");
}

}  // namespace

int main() {
    testDueInOrder();
    testDuplicates();
    testStale();
    testCap();
    testClear();
    testSequenceWrap();
    if (g_failures == 0) std::printf("timed_queue_test: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
