#pragma once
// The CD-specific stream clock rules (pure, unit tested; cd design adapter.md 10). A peer's stamps are mapped to our
// clock by the shared stream_clock::StreamClock (adapters/re0/src); on top of it the game's client clock is a frame clock
// that stops in menus while the network runs, so every injected timestamp is restamped to our own clock, a pause is
// told from a long frame, and player moves are thinned so the engine's 30-sample playout buffer never overflows.
#include <algorithm>
#include <cstdint>
#include <optional>

namespace stream_clock_rule {

constexpr int64_t kRestampPastMs = 500;      // an injected stamp is never older than now - 500 ms ...
constexpr int64_t kRestampFutureMs = 450;    // ... nor newer than now + 450 ms
constexpr uint64_t kPauseWindowMs = 600;     // our clock advancing under 1/4 of wall time over this long: paused
constexpr uint64_t kResumeWindowMs = 150;    // advancing at 1/2 of wall time over this long: running again
constexpr uint64_t kPauseRatioDenominator = 4;
constexpr uint64_t kResumeRatioDenominator = 2;
constexpr uint64_t kMoveSpacingMs = 22;      // at most one player Move per 22 ms ...
constexpr uint64_t kMoveSilenceMs = 150;     // ... except the first after 150 ms of silence

// The stamp an injected message carries: the peer's stamp mapped to our clock, clamped to [now - 500, now + 450] and
// strictly after the last stamp given to the same actor's buffer.
inline int64_t restamp(int64_t mappedMs, int64_t localNowMs, std::optional<int64_t> lastForActor) {
    int64_t stamp = std::clamp(mappedMs, localNowMs - kRestampPastMs, localNowMs + kRestampFutureMs);
    if (lastForActor && stamp <= *lastForActor) stamp = *lastForActor + 1;
    return stamp;
}

// Paused or running, from how far our own clock moved against wall time.
class PauseDetector {
public:
    bool paused() const { return paused_; }

    // Returns true when the state changed.
    bool sample(int64_t clockMs, uint64_t wallMs) {
        if (!windowStart_) {
            windowStart_ = Point{clockMs, wallMs};
            return false;
        }
        const uint64_t wall = wallMs - windowStart_->wallMs;
        const auto advanced = static_cast<uint64_t>(std::max<int64_t>(0, clockMs - windowStart_->clockMs));
        const uint64_t window = paused_ ? kResumeWindowMs : kPauseWindowMs;
        if (wall < window) return false;
        const bool change = paused_ ? advanced * kResumeRatioDenominator >= wall : advanced * kPauseRatioDenominator < wall;
        windowStart_ = Point{clockMs, wallMs};
        if (change) paused_ = !paused_;
        return change;
    }

private:
    struct Point {
        int64_t clockMs;
        uint64_t wallMs;
    };
    std::optional<Point> windowStart_;
    bool paused_ = false;
};

// Whether a player Move is sent: at most one per 22 ms, the first after 150 ms of silence always, and a parent change
// or an out-of-order stamp always.
class MoveThinner {
public:
    bool keep(uint64_t wallMs, uint64_t parentKey, int64_t stampMs) {
        const bool first = !lastSentWallMs_ || wallMs - *lastSentWallMs_ >= kMoveSilenceMs;
        const bool spaced = !lastSentWallMs_ || wallMs - *lastSentWallMs_ >= kMoveSpacingMs;
        const bool parentChanged = lastParent_ && *lastParent_ != parentKey;
        const bool outOfOrder = lastStampMs_ && stampMs < *lastStampMs_;
        lastParent_ = parentKey;
        lastStampMs_ = stampMs;
        if (!(first || spaced || parentChanged || outOfOrder)) return false;
        lastSentWallMs_ = wallMs;
        return true;
    }

private:
    std::optional<uint64_t> lastSentWallMs_;
    std::optional<uint64_t> lastParent_;
    std::optional<int64_t> lastStampMs_;
};

}  // namespace stream_clock_rule
