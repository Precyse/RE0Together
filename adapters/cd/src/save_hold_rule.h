#pragma once
// The save hold (pure, unit tested; cd design progression.md 5.2): while anything foreign lives in our world or a replay
// waits for its answer, autosaves are held so no save captures it. A hold lasts at most 30 s: at the bound every
// spawned shadow is ended, waiting replays are abandoned and the hold lets go until its reasons have cleared once. A
// save the player asked for is held at most 5 s while the shadows are ended, then passed.
#include <cstdint>
#include <optional>

namespace save_hold_rule {

enum Reason : uint32_t {
    kShadowAlive = 1,    // a spawned shadow is in our world
    kReplayWaiting = 2,  // a story or gimmick replay waits for its answer
    kLoading = 4,        // gate Loading or AwaitLocalReady
};

constexpr uint64_t kBoundMs = 30000;
constexpr uint64_t kManualMaxMs = 5000;

struct Step {
    bool holdAutosaves = false;   // keep SaveAutoReq back while true (posted when it ends)
    bool passManualSave = false;  // the held manual save goes out now
    bool endShadows = false;      // end every spawned shadow now
    bool abandonReplays = false;  // give up the replays still waiting
};

class Hold {
public:
    // A manual save (SaveToFileCurrentPlayerReq) seen now: true when it must be held (passed later by a Step).
    bool onManualSave(uint32_t reasons, uint64_t nowMs) {
        if (reasons == 0) return false;
        manualSinceMs_ = nowMs;
        return true;
    }

    Step tick(uint32_t reasons, uint64_t nowMs) {
        Step step;
        if (reasons == 0) {
            sinceMs_.reset();
            spent_ = false;
        } else if (!spent_) {
            if (!sinceMs_) sinceMs_ = nowMs;
            if (nowMs - *sinceMs_ >= kBoundMs) {
                step.endShadows = step.abandonReplays = true;
                spent_ = true;  // released until the reasons clear once
                sinceMs_.reset();
            }
        }
        step.holdAutosaves = reasons != 0 && !spent_;
        if (manualSinceMs_) {
            if (reasons & kShadowAlive) step.endShadows = true;
            if (reasons == 0 || nowMs - *manualSinceMs_ >= kManualMaxMs) {
                step.passManualSave = true;
                manualSinceMs_.reset();
            }
        }
        return step;
    }

    bool holding() const { return sinceMs_.has_value(); }

private:
    std::optional<uint64_t> sinceMs_;
    std::optional<uint64_t> manualSinceMs_;
    bool spent_ = false;
};

}  // namespace save_hold_rule
