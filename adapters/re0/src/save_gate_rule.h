#pragma once
#include <cstdint>
#include <optional>

#include "peer_sight.h"
#include "room_move_rule.h"

// Rule of the host's save gate (partner_mode=state, docs/RE0_NOTES.md "State mode: session"): a save is written from both
// characters' memory, so it waits until the partner stands in plain gameplay: think group 0 states 0..6, no door and no
// room script thread running. The saving machine's own character is not asked: the typewriter is a room script op
// (ScriptOp_save_point) that holds it in its own state while the save screen is open. A partner with no pose has no
// character in play (title, load list, loading), so there is nothing that changes. The save screen asks again every
// frame and has no other way out, so a refusal is bounded: `Patience` lets the request through after `kMaxWaitMs`.
// Pure, no game access, unit tested.
namespace save_gate_rule {

enum class Verdict : uint8_t { Plain, StalePose, PartnerBusy, PartnerInDoor, PartnerInScript };

constexpr const char* name(Verdict verdict) {
    switch (verdict) {
        case Verdict::Plain: return "plain gameplay";
        case Verdict::StalePose: return "partner pose is stale";
        case Verdict::PartnerBusy: return "partner is in an action";
        case Verdict::PartnerInDoor: return "partner is in a door";
        case Verdict::PartnerInScript: return "partner is in a room script";
    }
    return "unknown";
}

struct Situation {
    std::optional<peer_sight::Sight> pose;  // the partner's newest pose
    bool partnerInDoor;                     // its ROOM_STATE names a door target
    bool partnerInScript;                   // this machine follows a script thread the partner leads
};

constexpr Verdict judge(const Situation& situation, uint32_t nowMs) {
    if (!situation.pose) return Verdict::Plain;
    if (!peer_sight::fresh(*situation.pose, nowMs)) return Verdict::StalePose;
    if (situation.partnerInDoor) return Verdict::PartnerInDoor;
    if (situation.partnerInScript) return Verdict::PartnerInScript;
    if (!room_move_rule::freeMovement(situation.pose->group, situation.pose->state)) return Verdict::PartnerBusy;
    return Verdict::Plain;
}

constexpr uint32_t kMaxWaitMs = 3000;       // one refused save request waits this long, then goes through
constexpr uint32_t kRequestGapMs = 500;     // no ask for this long: the next ask is a new request (the screen asks every frame)

enum class Decision : uint8_t { Save, Wait, SaveUnsettled };

// How long the save screen's current request has been refused. Game thread only.
class Patience {
public:
    // The verdict for one ask at `nowMs`: Save (plain), Wait (refused, still within the limit) or SaveUnsettled (the
    // limit passed: the save goes through although the verdict is not Plain).
    Decision decide(Verdict verdict, uint32_t nowMs) {
        if (verdict == Verdict::Plain) {
            waiting_ = false;
            return Decision::Save;
        }
        if (!waiting_ || nowMs - lastAskMs_ > kRequestGapMs) {
            waiting_ = true;
            sinceMs_ = nowMs;
        }
        lastAskMs_ = nowMs;
        if (nowMs - sinceMs_ < kMaxWaitMs) return Decision::Wait;
        waiting_ = false;
        return Decision::SaveUnsettled;
    }

private:
    bool waiting_ = false;
    uint32_t sinceMs_ = 0;
    uint32_t lastAskMs_ = 0;
};

}  // namespace save_gate_rule
