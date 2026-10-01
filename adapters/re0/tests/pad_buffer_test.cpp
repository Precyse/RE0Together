#include <cstdio>

#include "../src/pad_buffer.h"

namespace {

int g_failures = 0;

void check(bool ok, const char* what) {
    if (ok) return;
    ++g_failures;
    std::printf("FAIL: %s\n", what);
}

using Buffer = PadBuffer<int>;

}  // namespace

int main() {
    Buffer buffer;
    int out = -1;
    size_t skipped = 0;

    buffer.add(1, 10);
    buffer.add(2, 20);
    check(buffer.advance(out, skipped) == Buffer::Step::Waiting, "waits until the target is buffered");
    buffer.add(3, 30);
    check(buffer.advance(out, skipped) == Buffer::Step::Consumed && out == 10, "replays the oldest frame first");
    buffer.add(1, 99);
    check(buffer.depth() == 2, "a frame already replayed is ignored");
    buffer.advance(out, skipped);
    buffer.advance(out, skipped);
    check(out == 30, "frames come out in order");
    check(buffer.advance(out, skipped) == Buffer::Step::Underrun && out == 30, "empty buffer is an underrun, last frame kept");
    check(buffer.target() == JitterTarget::kInitial + 1, "an underrun raises the target");

    Buffer behind;
    for (int frame = 1; frame <= 20; ++frame) behind.add(frame, frame);
    behind.advance(out, skipped);
    check(skipped == 20 - JitterTarget::kInitial && out == 20 - static_cast<int>(JitterTarget::kInitial) + 1,
          "far behind skips ahead to the target");

    Buffer full;
    for (int frame = 1; frame <= 100; ++frame) full.add(frame, frame);
    check(full.depth() == Buffer::kMaxBuffered, "buffer is capped");

    buffer.clear();
    check(buffer.depth() == 0 && buffer.target() == JitterTarget::kInitial, "clear resets frames and target");

    if (g_failures == 0) std::printf("pad_buffer_test: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
