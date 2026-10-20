#pragma once
#include <cstdint>
#include <optional>

#include "peer_sight.h"
#include "room_move_rule.h"

// Rule of the host's save gate (partner_mode=state, docs/RE0_NOTES.md "State mode: session"): a save is written from both
// characters' memory, so it waits until the partner stands in plain gameplay: think group 0 states 0..6, no door and no
// room script thread running. The saving machine's own character is not asked: the typewriter is a room script op
// (ScriptOp_save_point) that holds it in its own state while the save screen is open. Pure, no game access, unit tested.
namespace save_gate_rule {

enum class Verdict : uint8_t { Plain, NoPose, StalePose, PartnerBusy, PartnerInDoor, PartnerInScript };

constexpr const char* name(Verdict verdict) {
    switch (verdict) {
        case Verdict::Plain: return "plain gameplay";
        case Verdict::NoPose: return "no pose from the partner";
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
    if (!situation.pose) return Verdict::NoPose;
    if (!peer_sight::fresh(*situation.pose, nowMs)) return Verdict::StalePose;
    if (situation.partnerInDoor) return Verdict::PartnerInDoor;
    if (situation.partnerInScript) return Verdict::PartnerInScript;
    if (!room_move_rule::freeMovement(situation.pose->group, situation.pose->state)) return Verdict::PartnerBusy;
    return Verdict::Plain;
}

}  // namespace save_gate_rule
