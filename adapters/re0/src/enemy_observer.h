#pragma once
#include <cstdint>
#include <optional>

#include "enemy_observer_rule.h"
#include "enemy_observer_wire.h"

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

// Game thread: the engine's own decision, alignment and hit-replay paths of the input-mode design skip this enemy
// (state mode): it is shown from a stream (the peer runs the loaded room and the class is an Observer class, or the
// enemy was leased away), whether or not its construction is over, or this machine simulates it under a lease.
bool replicates(uintptr_t enemy);

// Game thread: the machine that simulates the enemy in pool slot `slot` of the loaded room right now, as this machine
// sees it: the holder of the lease on it, else the room authority. Unknown while the room is unowned or no session is
// linked.
enemy_observer_rule::Simulator simulatorOf(uint8_t slot);

// Game thread: the simulator's pool slot of the enemy in this machine's pool slot `slot`, as the stream bound it (a stream
// entry is bound to the local enemy by its spawn record, so the two pool slots may differ); what a HIT_REQUEST names.
// Nothing while the enemy is not shown from a stream.
std::optional<uint8_t> streamSlotOf(int slot);

// Game thread: the pool slot here of the enemy created from spawn record `record`, or -1.
int slotOfRecord(uint16_t record);

// Game thread, for the boss hand-over (phase 15): holds the enemy in pool slot `slot` under the CharPause scope whoever
// simulates it, so its class update stops while its motion, joints and hit shapes go on. False when the slot is empty.
// The hold ends with `startAt`, or when the room's role changes.
bool freeze(int slot);

// Game thread: the pose entry (class, spawn, HP, pose, record) of the enemy in pool slot `slot`, flagged alive or
// terminal, or nothing when the slot holds no Observer-class enemy created through the spawn hook.
std::optional<enemy_observer_wire::PoseEntry> snapshot(int slot);

// Game thread: carries the enemy in pool slot `slot` on from `state` on this machine (its pose and HP are put back, then
// the hand-over rule runs): the end of a lease on an enemy this machine gave away. False when the slot is empty.
bool resume(int slot, const enemy_observer_wire::PoseEntry& state);

// Game thread: ends the freeze of pool slot `slot` and carries the enemy on from `record` the way a hand-over does (the
// class's own setAction at step 0 when the record is an action). False when the slot is empty or unreadable.
bool startAt(int slot, const int32_t (&record)[4]);

}  // namespace enemy_observer
