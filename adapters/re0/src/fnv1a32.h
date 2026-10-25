#pragma once
#include <cstdint>
#include <span>

// FNV-1a 32 (pure, unit tested): the cheap equality digest of the world canary and of a save slot's bytes.
namespace fnv1a32 {

constexpr uint32_t kOffsetBasis = 2166136261u;
constexpr uint32_t kPrime = 16777619u;

// The hash of `bytes`, continued from `state` (kOffsetBasis for a new hash).
constexpr uint32_t hash(std::span<const uint8_t> bytes, uint32_t state = kOffsetBasis) {
    for (const uint8_t byte : bytes) state = (state ^ byte) * kPrime;
    return state;
}

}  // namespace fnv1a32
