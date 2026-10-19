#pragma once
#include <cstdint>

// Receiver side of the enemy observers (partner_mode=state, docs/RE0_NOTES.md "State mode: enemy observers"): when the
// other machine runs the loaded room, this machine shows that room's Observer-class enemies from its streams
// (ENEMY_POSE, ENEMY_MOTION) instead of simulating them. The engine's own CharPause path does the animating: for one
// enemy's unit move (slot 8, 0x526b30) the shown pose, HP, behaviour record and due motion requests are written and the
// CharPause bit is held, so the move runs only the motion update, the root-motion cancel and the joint and hit-shape
// updates and skips the class's update (AI, actions, movement, attacks). Nothing is ever decided here: with no data, a
// stale stream or a binding mismatch the enemy holds its last motion.
// The machine that runs the room is the simulator (enemy_observer_capture); one module tick tells the two roles apart.
namespace enemy_observer {

// Registers ENEMY_POSE and ENEMY_MOTION with the frame router (policy Any: the room authority is checked per batch),
// adds the simulator's capture, hooks the unit move and registers the per-frame tick. False when any part failed.
bool enable();

void uninstall();

// Game thread: this machine shows `enemy` from the peer's stream (state mode, the peer runs the loaded room, the class
// is an Observer class), whether or not its construction is over. The decision, alignment and hit replay paths of the
// input-mode design skip such an enemy.
bool observes(uintptr_t enemy);

}  // namespace enemy_observer
