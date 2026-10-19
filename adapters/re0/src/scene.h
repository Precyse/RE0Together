#pragma once
#include <cstdint>
#include <optional>

// The engine's room records (game.h "Scenes"): which room each character is in, and moving a character between
// rooms without touching the loaded room. Reads are exception-safe from any thread; `move` is game thread only.
namespace scene {

constexpr uint16_t kSceneIdLimit = 0xaf;  // scene ids are below this
constexpr uint16_t kNone = 0xffff;

// Scene id (the room argument doors pass) of the record `player` belongs to, or kNone.
uint16_t of(uintptr_t player);

// Scene id of the loaded room, or kNone.
uint16_t current();

// The load serial (load_serial.h): +1 whenever the loaded room record or its scene id changes, never 0. Any thread.
// Advances only while observeLoad runs (state mode registers it as a game tick callback).
uint16_t loadSerial();

// Game thread, once per game tick: feeds the loaded record to the load serial and logs a change.
void observeLoad();

// Moves `player` into room `sceneId` at door entry `entry` the way the engine's door carry does, but alone: the
// room's record is found (or loaded dormant), the player is put on the entry spot and assigned to it, and its old
// record is released when nobody is left in it. Into the loaded room the player appears at the door; out of it, it
// leaves the loaded room. False when the engine faulted. `spotMode` is the entry spot to stand on (spot_rule.h); without
// it the spot is chosen so that two characters in the room never share one.
bool move(uintptr_t player, uint16_t sceneId, uint32_t entry, std::optional<uint32_t> spotMode = std::nullopt);

// Puts `player` on the follower spot of door entry `entry` in its own room, the way a partner is placed behind the
// character that came through the door (a distinct spot when the follower spot equals the door spot). Game thread only.
// The spot mode used, or nothing when the engine faulted.
std::optional<uint32_t> placeBehind(uintptr_t player, uint32_t entry);

}  // namespace scene
