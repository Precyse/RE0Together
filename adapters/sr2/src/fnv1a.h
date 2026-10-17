#pragma once
#include <cstddef>
#include <cstdint>

// FNV-1a 64 (pure, unit tested): the digest of a look payload, and of anything else that needs a cheap equality check.
namespace fnv1a {

constexpr uint64_t kOffsetBasis = 0xCBF29CE484222325ull;
constexpr uint64_t kPrime = 0x100000001B3ull;

// The hash of `length` bytes, continued from `state` (kOffsetBasis for a new hash).
inline uint64_t hash64(const uint8_t* data, size_t length, uint64_t state = kOffsetBasis) {
    for (size_t i = 0; i < length; ++i) state = (state ^ data[i]) * kPrime;
    return state;
}

}  // namespace fnv1a
