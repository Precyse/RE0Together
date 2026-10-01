#pragma once
// The driver owns the vehicle's load. While a player drives, its machine reports what the vehicle's bed holds, by kind
// (VEHICLE_LOAD, on change and every few seconds); the other machine brings its copy of that vehicle's bed to the same
// kinds with the game's own calls, deleting extra pieces and creating missing ones. A vehicle the local player drives
// is never changed by reports.
#include <cstdint>

#include "net_client.h"
#include "protocol.h"

namespace vehicle_load {

constexpr uint16_t kMsgVehicleLoad = proto::kFirstGameType + 9;  // 0x0109, driver to all, reliable: VehicleLoad
constexpr uint32_t kMaxPieces = 64;

// A VEHICLE_LOAD payload: this header, then `count` u32 cargo kinds.
struct VehicleLoad {
    uint64_t vehicle;  // the game's vehicle id (VEHICLE_STATE's id)
    uint32_t count;
    uint32_t reserved;
};
static_assert(sizeof(VehicleLoad) == 16);

// Net thread.
void onFrame(const GameFrame& frame);
void tick(NetClient& net, const SessionSnapshot& session);

}  // namespace vehicle_load
