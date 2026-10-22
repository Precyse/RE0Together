#pragma once

// The boss authority hand-over (partner_mode=state, docs/RE0_NOTES.md "State mode: boss hand-over"). A boss fight has a
// few enemies that must change simulator together. When the machine that runs the room starts a door out of it with the
// other player still inside, it freezes its boss members under the enemy observer's CharPause scope, snapshots them
// (prepare), the other machine confirms it has every member with the same class and spawn (ready), the old authority
// commits, and once the room changes hands the new authority puts each member back at the snapshot and starts it at step 0
// (applied). A machine that takes over a room whose authority vanished commits alone from the members it last saw. The
// new simulator reports the members' alive mask (outcome). A hand-over that cannot finish keeps the members frozen and asks
// for a resync. Game thread except the frame handler, which only queues.
namespace boss_handover {

// Registers BOSS_HANDOVER with the frame router (policy Any: the sender is checked against the room authority) and the
// per-frame tick. Registered after the enemy observer's tick, so a room that changes hands is seen after the observer's
// own hand-over. False when the type could not be registered.
bool enable();

}  // namespace boss_handover
