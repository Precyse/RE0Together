#pragma once
#include <cstdint>

// Rule of the flag tracker across a local load (partner_mode=state, docs/RE0_NOTES.md "State mode: flags"): a load
// rewrites the whole flag block, so the diff against the flags both machines last agreed on is the save, not a story
// change, and must neither be proposed nor committed. From the load request until the loaded room's record is in (the
// room load serial advances) the tracker stands still; it then takes the loaded flags as its new baseline. Pure, no game
// access, unit tested.
namespace flag_load_hold_rule {

// Longest the tracker stands still when the load serial never advances (a load into the room record already loaded).
constexpr uint32_t kMaxHoldFrames = 1200;

class LoadHold {
public:
    // A load was requested while the room load serial was `loadSerial`, at `frame`.
    void begin(uint16_t loadSerial, uint32_t frame) {
        active_ = true;
        serial_ = loadSerial;
        startFrame_ = frame;
    }

    // True while the load has not finished: the room load serial is still the one of the request and the cap has not
    // passed. The first false answer ends the hold.
    bool holding(uint16_t loadSerial, uint32_t frame) {
        if (active_ && (loadSerial != serial_ || frame - startFrame_ >= kMaxHoldFrames)) active_ = false;
        return active_;
    }

    // The link dropped: nothing is tracked any more.
    void cancel() { active_ = false; }

private:
    bool active_ = false;
    uint16_t serial_ = 0;
    uint32_t startFrame_ = 0;
};

}  // namespace flag_load_hold_rule
