#pragma once
#include <cstdint>
#include <optional>

#include "event_place.h"
#include "event_rule.h"

// partner_mode=state: script ops that write one character's body (event_rule::OpRole::Body) run only where that
// character is owned. The remote-owned character is a puppet here (puppet::active), its body comes from its owner's
// stream, so event_sync skips the op on it and the owner's own run of the thread does it (docs/RE0_NOTES.md "State
// mode: scripts and cutscenes"). The subject is resolved the way the op's handler does, after the perspective swap.
namespace event_body {

// The op at a thread's pc, read by event_sync.
struct Op {
    uint16_t code;
    uint32_t subjectWord;  // 0x20000 controlled, 0x30000 partner, otherwise a unit ordinal; 0 for ops without a subject
    int32_t climbType;     // PlayerClimb / PlayerClimb_Pos only, else 0
    uint32_t scriptKey;    // the thread's script key (the scene id), the room the ordinal counts units in
};

// What sPlayer holds while the op runs: the controlled and the partner character.
struct Seen {
    uintptr_t controlled;
    uintptr_t partner;
};

struct Plan {
    event_rule::BodyRun run = event_rule::BodyRun::Run;
    bool hideControlled = false;  // RunLocalHalf: the puppet's slot of sPlayer reads 0 while the op runs
    bool hidePartner = false;
};

// Decides the op (and logs and counts a skip). `ownerRunsThread`: the peer follows this thread, or leads it.
Plan plan(const Op& op, Seen seen, bool ownerRunsThread);

// Before an op that may write the peer's copy of a character on its owner's behalf (SubMove for an absent owner, climb
// type 7): where the copy stands now. After the op, `event_place::reportMoved` tells the owner what became of it.
std::optional<event_place::Mark> markPlacement(const Op& op, Seen seen, const Plan& decision);

}  // namespace event_body
