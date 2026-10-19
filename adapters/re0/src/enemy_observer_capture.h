#pragma once
#include <cstdint>

// Simulator side of the enemy observers (partner_mode=state): the machine that runs a room's enemies sends the other
// machine what it needs to show them. ENEMY_MOTION: every cAnimControl::set call an Observer-class pool enemy makes,
// batched once a frame. ENEMY_POSE: every such enemy at 20 Hz, a dead one once more reliably (`terminal`) and a unit
// that left the pool once (`removed`).
namespace enemy_observer_capture {

// Adds the cAnimControl::set observer (pose_game installs its one hook at the first observer). False when the hook could
// not be installed or the observer list is full.
bool enable();

// Game thread, once per frame while this machine runs `scene` (room authority epoch `epoch`) with the peer in it:
// flushes the frame's motion requests and sends the pose round when it is due.
void tick(uint16_t scene, uint32_t epoch);

// Game thread: this machine stopped being the room's simulator (room left, authority lost, peer gone). Forgets what was
// sent; the next tick starts over.
void stop();

}  // namespace enemy_observer_capture
