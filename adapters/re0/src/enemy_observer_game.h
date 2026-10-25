#pragma once
#include <cstdint>

#include "enemy_observer_rule.h"
#include "enemy_observer_wire.h"
#include "game.h"

// The engine side of the enemy observers (partner_mode=state): reads and writes of one pool enemy, and the CharPause
// bit around one enemy's unit move. Every access is exception-safe; an unreadable enemy answers false.
namespace enemy_observer_game {

// Fills the engine fields of an ENEMY_POSE entry from `enemy`: HP, position, rotation, both motion layers, the
// behaviour record, hit attributes and +0x6a0c. The caller fills slot, class, flags, target and spawn.
bool read(uintptr_t enemy, enemy_observer_wire::PoseEntry& entry);

// Word 0 of the enemy's behaviour record (kSetupState while it is still constructing).
bool recordState(uintptr_t enemy, int32_t& state);

// Puts the enemy at a pose and sets its HP through setHP when it differs (a lease grant, the end of a lease).
void place(uintptr_t enemy, const float (&pos)[3], const float (&quat)[4], int32_t hp);

// Writes the shown sample into the enemy: position and rotation, HP (through setHP, only when it differs), the record
// words once the simulator's enemy has left its setup state, the hit attributes and +0x6a0c.
void write(uintptr_t enemy, const enemy_observer_rule::Sample& shown);

// Rebuilds the enemy's hit shapes from its joints the way its own update would (the observer skips that update), for
// the classes whose rebuild is known (game::kEnemyBaseHitShapeFunction); others are left as they are.
void refreshHitShapes(uintptr_t enemy);

// Puts back only what a hit test reads (HP and +0x6a0c) after the enemy's move changed it; position, record and motion
// are left to the stream.
void reassert(uintptr_t enemy, const enemy_observer_rule::Sample& shown);

// The CharPause bit (sGameInfo +0x50 bit 0) as found before one enemy's unit move.
struct CharPause {
    uintptr_t address = 0;  // 0 while the bit is not held by us
    bool wasSet = false;
};

// Sets the bit; false when sGameInfo is unreadable (nothing to release).
bool holdCharPause(CharPause& held);

// Puts the bit back as found: cleared unless a script had it set.
void releaseCharPause(CharPause& held);

}  // namespace enemy_observer_game
