#pragma once
#include <cstdint>

// The game's squared distance between two vec3s, for the thunks that compare it (milestone 16 wave 2).
namespace squared_distance {

// Calls the game function at `function` (sr2::kSquaredDistance or sr2::kSquaredDistanceTeam: eax and ecx are two vec3 pointers,
// ST0 the squared distance rounded to a float) and returns its float. The caller runs under an FpuGuard.
float __cdecl between(uintptr_t function, uint32_t first, uint32_t second);

}  // namespace squared_distance
