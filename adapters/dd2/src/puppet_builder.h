#pragma once
// A character built through the engine's stray-pawn route (coop-scratch\dd2\design\static_checks.md): a
// ContextWriterOnline (look.h) registered as an online product under an unused online CharacterID, then the generate
// request the riftstone summon makes, at a given place. Removed through GenerateManager.requestDestroy with the game's
// fade and without saving its context. The engine never removes a product, so one puppet keeps its id and product for
// the whole session: a rebuild swaps the product's writer and generates again. Every step is logged; the first refusal
// stops it.
#include <cstdint>
#include <optional>

namespace puppet_builder {

struct Puppet {
    uint32_t characterId = 0;
    void* product = nullptr;    // StrayPawnFactoryProductOnline, one reference of ours
    void* container = nullptr;  // the current generate request's GenerateInfoContainer, one reference of ours; null when none
};

// Game thread. Registers `writer` (the caller's, not released here) under a fresh online CharacterID.
std::optional<Puppet> create(void* writer);

// Game thread. The product's look for its next generation.
void rewrite(Puppet& puppet, void* writer);

// Game thread. Asks for the body at a place; false when a step refused.
bool generate(Puppet& puppet, const double position[3], float yaw);

// Game thread. Asks the engine to remove the body of a generate container (with the fade, no re-generate, its context
// not saved); false when refused.
bool destroy(void* container);

// Game thread. Asks for the body's removal and drops the container; the product stays for a later generate.
void despawn(Puppet& puppet);

// The engine removed the body on its own: drop the container without asking for a removal.
void bodyGone(Puppet& puppet);

// Drops every reference of ours (after despawn); the product stays registered with the engine.
void release(Puppet& puppet);

}  // namespace puppet_builder
