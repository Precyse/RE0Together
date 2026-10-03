#pragma once
// DS2-internal: keeps the marker manager's single player marker Sam's when the remote body exists (see
// ds2/remote_marker.cpp).
namespace remote_marker {

// After the remote's entity has been initialised: takes its player marker out of the marker manager and puts Sam's
// back as the player-marker group. Simulation thread.
void detachRemote();

}  // namespace remote_marker
