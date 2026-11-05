#pragma once
// The enemy roster, observe stage (DD2 design world.md "Identity" and "Roster", phase 7 probe P7-1). Every machine keeps
// the keys (roster_rule::Key, the UniqueID) of the enemies and monsters its engine registered. The host sends its roster
// as ENEMY_ROSTER once it has been settled for 3 s in plain gameplay (and to every joiner); a guest compares it with its
// own and logs how many match, are extra (its own lottery) or missing. The members and the host's keys feed the enemy
// sync (enemy_mirror.h, enemy_stream.h).
#include <cstdint>
#include <span>
#include <vector>

#include "roster_rule.h"

namespace roster {

// Installs the roster set; false (logged) when a row is missing or moved.
bool install();

// Game thread, every 30 Hz step.
void tick(uint64_t nowMs);

struct Member {
    roster_rule::Key key;
    void* component = nullptr;  // the registered Ch200000 or Monster
    void* character = nullptr;  // its app.Character
    bool monster = false;
};

// Game thread: this machine's registered enemies and monsters.
std::vector<Member> members();

// Guest: the host's newest roster, null before the first.
const std::vector<roster_rule::Key>* hostKeys();

// Our own roster has not changed for the settle time (3 s).
bool settled(uint64_t nowMs);

// Session routing: ENEMY_ROSTER from the host. False for a payload that does not decode.
bool onFrame(uint8_t slot, std::span<const uint8_t> payload);

}  // namespace roster
