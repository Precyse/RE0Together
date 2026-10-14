#pragma once
#include <cstdint>

// The two answers the identity and distance thunks of milestones 4 and 7 give instead of the game's partner getter.
namespace partner_identity {

// `compared` when it is a remote player human, else the default partner (under an override, the override's).
uintptr_t __cdecl match(uintptr_t compared);

// The remote player human nearest to the vec3 at `point`, 0 without one. Guards the FPU.
uintptr_t __cdecl nearest(uint32_t point);

}  // namespace partner_identity
