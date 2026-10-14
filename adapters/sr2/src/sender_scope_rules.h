#pragma once
#include <cstdint>

// The pure rules of the sender scope (milestone 9, unit tested).
namespace sender_scope_rules {

constexpr uint16_t kNoNetId = 0xFFFF;  // the net id lookup treats it as no object

// The net id a look message (player_morph 0x45, player_components 0x46) must name. A look can only describe the sender's
// own player, so it names the sender's human; the id in the payload (a stale or reissued one may name someone else) is only
// kept while the sender has no human with a net id. With 2 players the two are the same.
inline uint16_t lookNetId(uint16_t payloadId, uint16_t senderHumanId) {
    return senderHumanId != kNoNetId ? senderHumanId : payloadId;
}

}  // namespace sender_scope_rules
