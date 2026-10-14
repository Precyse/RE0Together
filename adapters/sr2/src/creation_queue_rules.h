#pragma once
#include <cstdint>

// The pure rules of the creation queue of a session member (unit tested): the objects whose creation is yet to be sent to
// that member. The game's own walk (0x8051F0, the join batch and G1) and the leave path (LV3) append to it.
namespace creation_queue_rules {

constexpr uint32_t kCapacity = 0x100;  // handles a member's queue holds

inline bool hasRoom(uint32_t count) { return count < kCapacity; }

// A record's created mask has a bit per member slot; queuing a creation for a member clears its bit.
inline uint16_t maskAfterQueue(uint16_t mask, uint8_t memberSlot) {
    return static_cast<uint16_t>(mask & ~(1u << memberSlot));
}

}  // namespace creation_queue_rules
