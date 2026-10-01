#pragma once
#include <cstddef>
#include <cstdint>
#include <map>

#include "jitter_target.h"

// The remote player's input frames between arrival and replay (pure logic, shared by net_pad and the offline
// trace_replay tool). Frames are replayed in frame-number order once JitterTarget frames are buffered; an empty
// buffer is an underrun (the last frame repeats); a buffer far behind skips ahead to the target.
template <class Frame>
class PadBuffer {
public:
    static constexpr size_t kMaxBuffered = 64;

    enum class Step { Waiting, Underrun, Consumed };

    void add(uint32_t number, const Frame& frame) {
        if (hasConsumed_ && number <= lastConsumed_) return;
        frames_.emplace(number, frame);
        while (frames_.size() > kMaxBuffered) frames_.erase(frames_.begin());
    }

    // One replay tick. On Consumed `out` holds the next frame; `skipped` counts frames dropped to catch up.
    Step advance(Frame& out, size_t& skipped) {
        skipped = 0;
        if (!started_ && frames_.size() >= jitter_.target()) started_ = true;
        if (!started_) return Step::Waiting;
        if (frames_.empty()) {
            started_ = false;
            jitter_.onUnderrun();
            return Step::Underrun;
        }
        jitter_.onFrameConsumed();
        if (frames_.size() > jitter_.maxBehind()) {
            for (size_t excess = frames_.size() - jitter_.target(); excess > 0; --excess) {
                frames_.erase(frames_.begin());
                ++skipped;
            }
        }
        const auto oldest = frames_.begin();
        out = oldest->second;
        lastConsumed_ = oldest->first;
        hasConsumed_ = true;
        frames_.erase(oldest);
        return Step::Consumed;
    }

    size_t depth() const { return frames_.size(); }
    size_t target() const { return jitter_.target(); }
    void clear() { *this = PadBuffer(); }

private:
    std::map<uint32_t, Frame> frames_;
    uint32_t lastConsumed_ = 0;
    bool hasConsumed_ = false;
    bool started_ = false;
    JitterTarget jitter_;
};
