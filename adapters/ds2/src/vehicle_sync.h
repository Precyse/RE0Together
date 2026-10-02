#pragma once
// The partner's driving: the driver owns the vehicle. While a player drives, its machine sends the vehicle's id and
// transform (VEHICLE_STATE); the other machine moves its own copy of that vehicle there on the simulation thread,
// extrapolated by the vehicle's speed between reports. When the reports stop, the copy stays where it was left. If
// both players claim the same vehicle, the one that claimed it first drives it and the other rides along: the
// passenger's machine moves its copy by the driver's reports (it keeps no driver of its own), and its report says
// so (role), so the driver's machine seats the partner's body beside its player.
#include <cstdint>
#include <optional>

#include "net_client.h"
#include "protocol.h"

namespace vehicle_sync {

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

// Net thread: the partner's reports, and sending the local player's vehicle.
void onFrame(const GameFrame& frame);
void tick(NetClient& net, const SessionSnapshot& session);

// Simulation thread (main_thread tick): moves the vehicles partners are driving.
void place();

struct Riding {
    uint64_t id;    // the vehicle
    uint32_t role;  // kRoleDriver or kRolePassenger
};

// The vehicle the partner in `slot` is in now (its reports are fresh), if any. Any thread.
std::optional<Riding> partnerRiding(uint8_t slot);

}  // namespace vehicle_sync
