#pragma once
// The partner's driving: the driver owns the vehicle. While a player drives, its machine sends the vehicle's id and
// transform (VEHICLE_STATE); the other machine moves its own copy of that vehicle there on the simulation thread,
// extrapolated by the vehicle's speed between reports. When the reports stop, the copy stays where it was left. If
// both players claim the same vehicle, the one that claimed it first drives it and the other rides along: the
// passenger's machine moves its copy by the driver's reports (it keeps no driver of its own), and its report says
// so (role, see vehicle_wire.h), so the driver's machine seats the partner's body beside its player. The local player
// riding the partner's vehicle through setdriver_guard reports the same way.
#include <cstdint>
#include <optional>

#include "net_client.h"
#include "protocol.h"
#include "vehicle_wire.h"

namespace vehicle_sync {

// Net thread: the partner's reports, and sending the local player's vehicle (driven, or ridden as a passenger).
void onFrame(const GameFrame& frame);
void tick(NetClient& net, const SessionSnapshot& session);

// Simulation thread (sim_tick): moves the vehicles partners are driving.
void place();

// Start-up: registers `place` on the simulation thread's per-frame tick.
void installEarly();

struct Riding {
    uint64_t id;    // the vehicle
    uint32_t role;  // kRoleDriver or kRolePassenger
};

// The vehicle the partner in `slot` is in now (its reports are fresh), if any. Any thread.
std::optional<Riding> partnerRiding(uint8_t slot);

}  // namespace vehicle_sync
