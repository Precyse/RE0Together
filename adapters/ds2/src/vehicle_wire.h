#pragma once
// VEHICLE_STATE: the vehicle a player is in and where it is, sent by the machine of the player in it. The driver owns the
// vehicle and the receiver moves its copy there. A passenger (the second to get in, or the local player riding the
// partner's vehicle) sends the same message with the passenger role and the vehicle's current pose: its receiver, the
// driver's machine, seats the passenger's body in the second pod and never moves the vehicle by it.
#include <cstdint>
#include <cstring>
#include <span>

#include "protocol.h"

namespace vehicle_wire {

constexpr uint16_t kMsgVehicleState = proto::kFirstGameType + 8;  // 0x0108, driver or passenger to all, unreliable
constexpr float kSendHz = 30.0f;
constexpr uint32_t kRoleDriver = 0;
constexpr uint32_t kRolePassenger = 1;

struct VehicleState {
    uint32_t seq;
    uint32_t role;        // kRoleDriver, or kRolePassenger when the sender rides a vehicle the receiver drives
    uint64_t id;          // the game's vehicle id, the same in both worlds
    float position[3];    // world metres
    float rotation[3][3]; // rows: right, forward, up
};
static_assert(sizeof(VehicleState) == 64);

// False for a payload of the wrong size or an unknown role; `out` is then untouched.
inline bool decode(std::span<const uint8_t> payload, VehicleState& out) {
    if (payload.size() != sizeof(VehicleState)) return false;
    VehicleState state;
    std::memcpy(&state, payload.data(), sizeof(state));
    if (state.role > kRolePassenger) return false;
    out = state;
    return true;
}

}  // namespace vehicle_wire
