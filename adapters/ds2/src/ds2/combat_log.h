#pragma once
// DS2-internal: one log line per damage that touches an enemy of the directory or a player's body, with who hit whom, the
// damage type resource, the part, the amount ApplyDamage uses (DamageParams +0x78) and the victim's health before and
// after (0-254 of the maximum). Used by the ApplyDamage hook (ds2/combat_hook.cpp). Any thread.
#include <cstdint>

namespace combat_log {

struct Snapshot {
    bool tracked = false;
    uint8_t healthBefore = 0;
    uintptr_t attacker = 0;
};

// Reads the attacker and the victim's health ahead of the apply; `tracked` is false for damage nobody cares about.
Snapshot before(uintptr_t victim, uintptr_t params);

// Logs the hit with `outcome` ("applied", "diverted to the host", ...) and the victim's health now.
void after(const Snapshot& snapshot, uintptr_t victim, uintptr_t params, const char* outcome);

}  // namespace combat_log
