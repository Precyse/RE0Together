#pragma once
#include <cstdint>

// What the newest POSE_STATE of the driving peer says about it (partner_mode=state): the room and load it stands in, its
// think group and state and when the pose arrived. Pure, no game access. The session handshake and the save gate read it
// instead of the puppet's copy of the peer's character, which is written only while the peer is in the loaded room.
namespace peer_sight {

constexpr uint32_t kFreshMs = 2000;  // a pose older than this says nothing about where the peer is now

struct Sight {
    uint16_t room;        // scene id of the peer's loaded room
    uint16_t loadSerial;  // its load serial (load_serial.h)
    uint8_t group;        // think group, state
    uint8_t state;
    uint32_t arrivalMs;   // the local clock when the pose arrived
};

// Wrap-safe; an arrival stamped a moment after `nowMs` (another thread) is fresh.
constexpr bool fresh(const Sight& sight, uint32_t nowMs) {
    return static_cast<int32_t>(nowMs - sight.arrivalMs) <= static_cast<int32_t>(kFreshMs);
}

}  // namespace peer_sight
