#pragma once
#include <array>
#include <cstdint>

// Pure: the random state an enemy's creation starts from (no game access, unit tested). The game rolls its one random
// generator (xorshift128, four words) while it creates an enemy (the base family's spawn init 0x420ce0 picks the
// variant +0x6ab0 = rand % 3 on a first spawn), so two machines would create different enemies from the same spawn
// record. Both start the creation from a state made of what they share: the scene, the record's index, kind and spawn
// id.
namespace spawn_seed {

using State = std::array<uint32_t, 4>;

constexpr uint32_t kGolden = 0x9e3779b9u;  // spreads the four words apart

// A 32-bit avalanche mix (every input bit changes about half the output bits).
constexpr uint32_t mix(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

// Never all zero: an all-zero xorshift state only ever yields zero.
constexpr State stateFor(uint16_t scene, uint16_t index, uint32_t kind, uint32_t spawnId) {
    const uint32_t key = mix(mix(mix((static_cast<uint32_t>(scene) << 16) | index) ^ kind) ^ spawnId);
    State state{};
    for (uint32_t i = 0; i < state.size(); ++i) state[i] = mix(key + kGolden * (i + 1));
    state[0] |= 1u;
    return state;
}

}  // namespace spawn_seed
