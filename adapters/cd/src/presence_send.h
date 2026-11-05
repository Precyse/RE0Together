#pragma once
// This player as the other players see it (cd design players.md 2.1 and 3): the own character template captured from
// the ActorLoadDataAck pair the server sends at every load, and the own MoveActorReq, StartActionReq and RotateActorReq
// as they leave our client. Also the own clock anchor: the newest timestamp our client stamped on a Req, which is the
// clock domain every injected Ack must use.
#include <cstdint>
#include <optional>

#include "funnels.h"

namespace presence_send {

// After the catalogue: asks the funnels for the payloads it needs. False when a name is missing.
bool watchOpcodes();

// Main tick: every funnel event, then the tick.
void onEvent(const funnels::Event& event, uint64_t nowMs);
void tick(uint64_t nowMs, bool peerJoined);

// The own clock now, in the domain of our client's Req timestamps; null until our client sent one.
std::optional<uint64_t> ownStampNow(uint64_t nowMs);

// True once our own Move and Action Reqs were seen at the sizes the recipes expect (presence_rule.h); false (and
// logged once) when this build's sizes differ.
bool recipesMatch();

// The own keys from the template (zero before one), for the receiver's key barrier.
uint32_t ownUserKey();
uint32_t ownPlayerKey();

// The own template generation (0 before the first); every peer copy of us starts from this template.
uint16_t generation();

struct Stats {
    uint32_t templatesSent = 0, movesSent = 0, actionsSent = 0;
};
Stats stats();

}  // namespace presence_send
