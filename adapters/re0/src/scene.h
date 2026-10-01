#pragma once
#include <cstdint>

// The engine's room records (game.h "Scenes"): which room each character is in, and moving a character between
// rooms without touching the loaded room. Reads are exception-safe from any thread; `move` is game thread only.
namespace scene {

constexpr uint16_t kNone = 0xffff;  // scene ids are below 0xaf

// Scene id (the room argument doors pass) of the record `player` belongs to, or kNone.
uint16_t of(uintptr_t player);

// Scene id of the loaded room, or kNone.
uint16_t current();

// Moves `player` into room `sceneId` at door entry `entry` the way the engine's door carry does, but alone: the
// room's record is found (or loaded dormant), the player is put on the entry spot and assigned to it, and its old
// record is released when nobody is left in it. Into the loaded room the player appears at the door; out of it, it
// leaves the loaded room. False when the engine faulted.
bool move(uintptr_t player, uint16_t sceneId, uint32_t entry);

}  // namespace scene
