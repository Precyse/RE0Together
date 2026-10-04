#pragma once
// Decima Entity helpers shared by every Decima game (field offsets from the engine's RTTI).
#include <cstdint>
#include <functional>

namespace decima {

// The entity's first component for which `matches(component)` is true, or 0. Safe on half-built or freed entities.
uintptr_t findComponentWhere(uintptr_t entity, const std::function<bool(uintptr_t component)>& matches);

// The entity's first component whose primary vtable is `vtable`, or 0. Safe on half-built or freed entities.
uintptr_t findComponent(uintptr_t entity, uintptr_t vtable);

}  // namespace decima
