#pragma once
#include <cstdint>

// DS2-internal view of the world's vehicles shared by the ds2/ files.
namespace ds2 {

// The entity of the loaded vehicle with this id (the game's own id, saved with the world), or 0 when it is not loaded.
uintptr_t loadedVehicle(uint64_t id);

}  // namespace ds2
