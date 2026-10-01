#pragma once
// Decima Entity helpers shared by every Decima game (field offsets from the engine's RTTI).
#include <cstdint>

namespace decima {

// The entity's first component whose primary vtable is `vtable`, or 0. Safe on half-built or freed entities.
uintptr_t findComponent(uintptr_t entity, uintptr_t vtable);

}  // namespace decima
