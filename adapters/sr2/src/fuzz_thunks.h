#pragma once
#include <cstdint>

#include "partner_sites.h"
#include "partner_thunks.h"

// The code behind the sites of fuzz_sites.h (milestone 17 wave 3): the FUZZ level, crime and riot code asks every remote where the game
// asked the one default partner. With fewer than two remotes, or a pairing that cannot be trusted, each thunk does what the replaced
// code did.
namespace fuzz_thunks {

// {nullptr, nullptr} for a thunk that is not one of FUZZ's own (FZ1 and FZ12 reuse the diversion and NPC AI thunks).
partner_thunks::Binding bind(partner_sites::Thunk thunk);

// Starts the generator that picks the remote a crime NPC attacks. Called once at install.
void seed(uint32_t value);

}  // namespace fuzz_thunks
