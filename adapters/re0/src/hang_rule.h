#pragma once
#include <cstdint>

// When the game counts as hung (docs/RE0_NOTES.md "Hang watchdog"): the render thread beats once per drawn frame and a
// watcher polls. Pure, no game access, unit tested. All times are wrapping 32-bit milliseconds.
namespace hang_rule {

constexpr uint32_t kHangMs = 20'000;  // time without a frame, while the game window is foreground, that is a hang

enum class Verdict : uint8_t { Wait, Dump };

// One stall gives one Dump. Stall time only accumulates while the window is foreground (a background game may be
// throttled by Windows); losing focus keeps the time already counted, a beat clears it and re-arms the next Dump.
class HangWatch {
public:
    // A frame was drawn at `nowMs`.
    void beat(uint32_t nowMs) {
        armed_ = true;
        dumped_ = false;
        stalledMs_ = 0;
        lastMs_ = nowMs;
    }

    // Dump once the accumulated stall reaches kHangMs; Wait before the first beat, after the Dump and below the limit.
    Verdict check(uint32_t nowMs, bool foreground) {
        if (!armed_) return Verdict::Wait;
        const uint32_t elapsed = nowMs - lastMs_;
        lastMs_ = nowMs;
        if (!foreground || dumped_) return Verdict::Wait;
        stalledMs_ += elapsed;
        if (stalledMs_ < kHangMs) return Verdict::Wait;
        dumped_ = true;
        return Verdict::Dump;
    }

private:
    bool armed_ = false;
    bool dumped_ = false;
    uint32_t stalledMs_ = 0;
    uint32_t lastMs_ = 0;  // the last beat or check
};

}  // namespace hang_rule
