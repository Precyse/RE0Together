#pragma once
#include <cstdint>

// Size of a co-op player pool for `maxPlayers`. The shipped pool holds one guest's data (co-op is host + 1 guest), so
// it scales with the number of guests. Returns 0 when the result does not fit 32 bits or there are no guests.
inline uint32_t scaledPoolSize(uint32_t originalSize, uint32_t maxPlayers) {
    if (maxPlayers < 2) return 0;
    const uint64_t size = static_cast<uint64_t>(originalSize) * (maxPlayers - 1);
    return size > UINT32_MAX ? 0 : static_cast<uint32_t>(size);
}
