#pragma once
#include <cstdint>

#include "stream_clock.h"

// When the owner side of the pose puppet samples and resends (pure, unit tested).
namespace pose_capture_rule {

constexpr uint32_t kSampleIntervalMs = 33;  // 30 Hz
constexpr uint32_t kHeldAfterMs = 250;      // no sample for this long: the owner's world is not moving
constexpr uint32_t kHeldIntervalMs = 250;   // 4 Hz resend while it is not

// A time accumulator: one sample per interval however many moves the game runs in it (60 or 30 frames a second).
class SampleTimer {
public:
    // True when a sample is due at `nowMs`; a late move does not owe the missed samples.
    bool due(uint32_t nowMs) {
        if (!started_) {
            started_ = true;
            next_ = nowMs + kSampleIntervalMs;
            return true;
        }
        if (stream_clock::after(next_, nowMs)) return false;
        next_ += kSampleIntervalMs;
        if (!stream_clock::after(next_, nowMs)) next_ = nowMs + kSampleIntervalMs;
        return true;
    }

private:
    bool started_ = false;
    uint32_t next_ = 0;
};

// Decides when the last sample is sent again with the `held` flag.
class HeldResend {
public:
    // A real sample was taken at `nowMs`.
    void onSample(uint32_t nowMs) {
        lastSampleMs_ = nowMs;
        lastResendMs_ = nowMs;
        hasSample_ = true;
    }

    // True when a held resend is due at `nowMs`: a sample exists, none for `kHeldAfterMs`, none resent for `kHeldIntervalMs`.
    bool due(uint32_t nowMs) {
        if (!hasSample_ || static_cast<uint32_t>(nowMs - lastSampleMs_) < kHeldAfterMs ||
            static_cast<uint32_t>(nowMs - lastResendMs_) < kHeldIntervalMs) {
            return false;
        }
        lastResendMs_ = nowMs;
        return true;
    }

private:
    bool hasSample_ = false;
    uint32_t lastSampleMs_ = 0;
    uint32_t lastResendMs_ = 0;
};

}  // namespace pose_capture_rule
