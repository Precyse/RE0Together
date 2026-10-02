#pragma once
// DS2-internal: Vehicle::SetDriver treats every player-category driver key as the local player (it sets the vehicle
// manager's driven vehicle and last-driven ids). When the remote enters or leaves a vehicle those fields are saved
// and restored, so the game does not think Sam drives. Installed early.
namespace setdriver_guard {

void install();

}  // namespace setdriver_guard
