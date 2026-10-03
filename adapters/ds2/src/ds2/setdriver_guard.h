#pragma once
// DS2-internal: Vehicle::SetDriver treats every player-category driver key as the local player (it sets the vehicle
// manager's driven vehicle and last-driven ids). When the remote enters or leaves a vehicle those fields are saved
// and restored, so the game does not think Sam drives. Installed early.
//
// The local player can ride as a passenger on a vehicle the partner drives: the game's "free to use" gate is opened for
// that vehicle only, and Sam's own enter and leave are answered as done without making him its driver.
#include <cstdint>

namespace setdriver_guard {

void install();

// The id of the vehicle the local player rides in as a passenger, 0 when none. Any thread.
uint64_t localPassengerVehicle();

// The local player is not riding any more (the ride states ended on their own).
void endLocalPassenger();

}  // namespace setdriver_guard
