#pragma once
// DS2-internal: field offsets of the DSPlayerRideVehicleActionPlugin (ds2::ridePlugin) and of the owner request it reads,
// shared by remote_ride and ride_probe (docs/DS2_NOTES.md, "Riding the peer's vehicle with the game's own states").
#include <cstdint>

namespace ride_plugin {

constexpr uintptr_t kPhase = 0x118;          // 0 on foot, 1 RideOn, 2 Drive, 3 RideOff
constexpr uintptr_t kRequestedPhase = 0x11a;
constexpr uintptr_t kTarget = 0x220;        // the vehicle entity id the ride starts
constexpr uintptr_t kOwner = 0x38;           // the player's action state that holds the request
// The owner's ride request: the engine's own TryStart accepts a request kind of 3 or 9 with a vehicle's entity id.
constexpr uintptr_t kOwnerRequestKind = 0x3970;
constexpr uintptr_t kOwnerRequestTarget = 0x39e0;

}  // namespace ride_plugin
