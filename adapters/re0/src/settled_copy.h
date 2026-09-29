#pragma once
#include <array>
#include <chrono>
#include <cstdint>

// Change tracking for a fixed-size block of game memory: a block is due for sending once it has differed from the
// last sent copy and stayed unchanged for kSettleFrames, or when the last send is older than the resend interval.
// Pure logic, no game access.
constexpr uint32_t kSettleFrames = 10;

inline uint64_t monotonicMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

template <size_t N>
class SettledCopy {
public:
    using Bytes = std::array<uint8_t, N>;

    // Records this frame's contents. True when they differ from the previous observation (never on the first one).
    bool observe(const Bytes& current, uint32_t frame) {
        const bool changed = observed_ && current != current_;
        if (!observed_ || changed) changedFrame_ = frame;
        current_ = current;
        observed_ = true;
        return changed;
    }

    bool initialised() const { return observed_; }

    // resendAfterMs of 0 disables the periodic resend.
    bool due(uint32_t frame, uint64_t nowMs, uint64_t resendAfterMs) const {
        if (!observed_) return false;
        if (!sent_ || current_ != sentCopy_) return frame - changedFrame_ >= kSettleFrames;
        return resendAfterMs != 0 && nowMs - sentAtMs_ >= resendAfterMs;
    }

    const Bytes& current() const { return current_; }

    void markSent(uint64_t nowMs) {
        sentCopy_ = current_;
        sentAtMs_ = nowMs;
        sent_ = true;
    }

    // Treats `bytes` as both the observed and the sent state: nothing is due until they change again.
    void adopt(const Bytes& bytes, uint64_t nowMs) {
        current_ = bytes;
        observed_ = true;
        markSent(nowMs);
    }

    void reset() { *this = SettledCopy(); }

private:
    Bytes current_{};
    Bytes sentCopy_{};
    bool observed_ = false;
    bool sent_ = false;
    uint32_t changedFrame_ = 0;
    uint64_t sentAtMs_ = 0;
};
