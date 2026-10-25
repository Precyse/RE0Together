#pragma once
#include <cstdint>

#include "enemy_observer_wire.h"

// Simulator side of the enemy observers (partner_mode=state): the machine that runs a room's enemies sends the other
// machine what it needs to show them. ENEMY_MOTION: every cAnimControl::set call an Observer-class pool enemy makes,
// batched once a frame. ENEMY_POSE: every such enemy at 20 Hz, a dead one once more reliably (`terminal`) and a unit
// that left the pool once (`removed`).
namespace enemy_observer_capture {

// The pose entry of the Observer-class enemy `enemy` standing in pool slot `slot`: engine fields, class, target and spawn
// (the flags are the caller's). False when the slot holds no such enemy created through the spawn hook, or it is unreadable.
bool entryFor(int slot, uintptr_t enemy, enemy_observer_wire::PoseEntry& entry);

// Adds the cAnimControl::set observer (pose_game installs its one hook at the first observer). False when the hook could
// not be installed or the observer list is full.
bool enable();

// Game thread, while this machine streams: one reliable ENEMY_POSE round of every enemy it streams, in the state it has
// now, so the machine that takes the room over adopts it (the first tick of this machine's own door out of the room).
void sendFinal(uint32_t nowMs);

// Game thread, once per frame while this machine streams enemies of `scene` (room authority epoch `epoch`) to the peer:
// either it runs the room (`simulatesRoom`: every enemy it has not given away) or it holds leases (only those enemies,
// named by their token). Flushes the frame's motion requests and sends the pose round when it is due.
void tick(uint16_t scene, uint32_t epoch, bool simulatesRoom);

// Game thread: this machine stopped being the room's simulator (room left, authority lost, peer gone). Forgets what was
// sent; the next tick starts over.
void stop();

}  // namespace enemy_observer_capture
