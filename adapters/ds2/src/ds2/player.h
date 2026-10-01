#pragma once
#include <cstdint>

// DS2-internal view shared by the ds2/ files: the local player's entity (DSPlayerEntity), or 0.
namespace ds2 {

uintptr_t localPlayerEntity();

}  // namespace ds2
