#pragma once
// DS2-internal: the DSGameState bit words (global 0x14623E4C0): cutscene, menu, loading and area-change states.
#include <cstdint>

#include "decima/safe_read.h"
#include "ds2/engine.h"

namespace game_state {

constexpr uintptr_t kGlobal = 0x14623E4C0;
constexpr uintptr_t kBitsLow = 0x160, kBitsHigh = 0x168;

// Bit words of the live state object; false while it does not exist yet. Any thread.
inline bool read(uint64_t& low, uint64_t& high) {
    const uintptr_t state = decima::readPointer(ds2::at(kGlobal));
    if (!state) return false;
    low = ds2::field<uint64_t>(state, kBitsLow);
    high = ds2::field<uint64_t>(state, kBitsHigh);
    return true;
}

}  // namespace game_state
