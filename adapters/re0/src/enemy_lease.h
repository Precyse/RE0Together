#pragma once
#include <cstdint>
#include <optional>

#include "contact_rule.h"
#include "enemy_lease_rule.h"
#include "enemy_observer_wire.h"

// The grab lease (partner_mode=state, docs/RE0_NOTES.md "State mode: contact and lease"). A paired attack (a hold, a
// bite, a carry) binds the victim to the enemy through the victim's own think, the struggle reads the victim's own pad
// and the paired snap needs the enemy next to the victim, so for its duration the enemy is simulated on the victim's
// machine: the room authority grants the lease when its enemy reaches the remote character (ENEMY_LEASE) and shows the
// enemy from the holder's stream; the holder starts the granted action natively against its own character, streams the
// enemy back (ENEMY_POSE / ENEMY_MOTION, flagged leased, named by the token) and ends the lease when the hold is over
// (ENEMY_LEASE_END). The authority resumes the enemy from the final state at step 0. All of it runs on the game thread
// except the frame handlers, which only queue.
namespace enemy_lease {

using enemy_lease_rule::Role;

// Registers ENEMY_LEASE and ENEMY_LEASE_END with the frame router (policy Any: the sender is checked against the room
// authority or the lease) and the per-frame tick. Registered before the enemy observer's tick, so a lease granted or
// started in a frame scopes or frees its enemy in the same frame.
bool enable();

// Game thread: the role of the enemy in pool slot `slot` of the loaded room.
Role roleOf(uint8_t slot);

// Game thread: the other machine of the lease on `slot` (the holder for Away, the authority for Held).
std::optional<uint8_t> peerOf(uint8_t slot);

bool anyAway();
bool anyHeld();

// Game thread: the low 16 bits of the token of the lease this machine holds on `slot`, or 0.
uint16_t tokenOf(uint8_t slot);

// Game thread, the router: a lease or a contact waiting to become one stands on this enemy / on this enemy and victim.
bool busy(uint8_t slot);
bool busyWith(uint8_t slot, uint8_t victim);

// Game thread, the router: the enemy in `contact.slot` made a paired contact (reaction state `state`) on a puppet. The
// grant is made on the next tick, when the enemy's update is over and its record is the granted action.
void request(const contact_rule::Contact& contact, uintptr_t enemy, int32_t state);

// Game thread: a streamed pose of the holder for an enemy this machine gave away. True when the pose may be shown; a
// pose that names another token, comes from another machine, heals the enemy or moves it too far is counted and refused.
bool acceptPose(const enemy_observer_wire::PoseEntry& entry, uint8_t sender, uint32_t nowMs);

// Game thread: the room changed, the peer left or the session ended: every lease and queued grant is dropped. The
// enemies given away are resumed by the caller (enemy_observer) before this runs.
void reset();

}  // namespace enemy_lease
