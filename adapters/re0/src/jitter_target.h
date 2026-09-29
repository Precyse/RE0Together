#pragma once
#include <cstddef>
#include <cstdint>

// How many remote pad frames to hold before replaying them (pure logic, unit tested). An underrun means the
// network delivered late, so the target grows by one frame; after a calm stretch it shrinks by one, so the added
// input delay follows the link's actual jitter.
class JitterTarget {
public:
    static constexpr size_t kMin = 2;
    static constexpr size_t kMax = 8;
    static constexpr size_t kInitial = 3;
    static constexpr uint32_t kCalmFrames = 600;  // ~10 s at 60 fps without an underrun
    static constexpr size_t kSlack = 6;           // beyond target + slack the replay skips ahead

    size_t target() const { return target_; }
    size_t maxBehind() const { return target_ + kSlack; }

    void onUnderrun() {
        if (target_ < kMax) ++target_;
        calmFrames_ = 0;
    }

    void onFrameConsumed() {
        if (++calmFrames_ < kCalmFrames) return;
        calmFrames_ = 0;
        if (target_ > kMin) --target_;
    }

    void reset() { *this = JitterTarget(); }

private:
    size_t target_ = kInitial;
    uint32_t calmFrames_ = 0;
};
