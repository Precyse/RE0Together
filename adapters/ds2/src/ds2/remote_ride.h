#pragma once
// DS2-internal: the remote player boards the vehicle its partner drives and leaves it when the partner stops, through
// the game's own states: a ride request on the remote's DSPlayerRideVehicleActionPlugin makes DSPlayerState::Update run
// RideOn, Drive and RideOff (with the seated animation, SetDriver and the parent link), exactly as for Sam.
namespace remote_ride {

// Simulation thread, once per frame while the remote is live.
void tick();

// True while the remote boards, rides or leaves: the vehicle moves it, so it must not follow the partner's pose.
bool holdsBody();

}  // namespace remote_ride
