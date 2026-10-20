#pragma once
#include <cstdint>

#include "contact_rule.h"
#include "enemy_observer_wire.h"

// PLAYER_HIT (partner_mode=state): a plain hit (flinch, knock-back, knock-down, death) that the machine simulating an
// enemy turned into a message instead of a call on the other machine's puppet. The victim's owner calls the engine's own
// damage entry with its local copy of the enemy at the sender's time plus the render delay, so the hit lands when its
// copy of the enemy visibly reaches the character.
namespace contact_net {

// Registers PLAYER_HIT with the frame router (policy Any: the sender is checked against the enemy's simulator) and the
// per-frame tick that applies the hits that are due. False when the registration failed.
bool enable();

// Game thread: sends `contact` of the enemy described by `enemy` (class, spawn, slot) to the victim's owner.
void sendHit(const contact_rule::Contact& contact, const enemy_observer_wire::PoseEntry& enemy);

}  // namespace contact_net
