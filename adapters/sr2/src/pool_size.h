#pragma once
#include <cstdint>

#include "sr2_addresses.h"

// Size of a co-op player pool for `maxPlayers`. The shipped pool holds one guest's data (co-op is host + 1 guest), so
// it scales with the number of guests. Returns 0 when the result does not fit 32 bits or there are no guests.
inline uint32_t scaledPoolSize(uint32_t originalSize, uint32_t maxPlayers) {
    if (maxPlayers < 2) return 0;
    const uint64_t size = static_cast<uint64_t>(originalSize) * (maxPlayers - 1);
    return size > UINT32_MAX ? 0 : static_cast<uint32_t>(size);
}

namespace coop_allocators_size {

// Deltacomp bundles whose element count does not depend on max_players, with the counts and element bytes of the
// 1-peer (N=1) and 8-peer (N=8) instances (docs/SR2_NOTES.md, "Replication").
struct Bundle {
    uint32_t n1Count;
    uint32_t n1Element;
    uint32_t n8Count;
    uint32_t n8Element;
};

constexpr uint32_t kHistoryDepth = 15;  // deltacomp history entries per npc and per player
constexpr uint32_t kNpcCount = kHistoryDepth * sr2::kCoopNpcLimit;

constexpr Bundle kFixedBundles[] = {
    {kNpcCount, 0x1E4, kNpcCount, 0x848},  // npc
    {1920, 0x8C, 1920, 0x23C},             // item
    {2400, 0x8C, 2400, 0x23C},             // mover
    {30, 0x7C, 30, 0x1F4},                 // prop
    {480, 0x94, 780, 0x260},               // pipebomb
    {480, 0x7C, 780, 0x1F4},               // rocket
    {480, 0x204, 480, 0x8D8},              // vehicle
};

constexpr uint64_t fixedBytes(bool n8) {
    uint64_t total = 0;
    for (const Bundle& bundle : kFixedBundles) {
        total += n8 ? uint64_t{bundle.n8Count} * bundle.n8Element : uint64_t{bundle.n1Count} * bundle.n1Element;
    }
    return total;
}

constexpr uint32_t kPlayerN1Element = 0x220;
constexpr uint32_t kPlayerN8Element = 0x928;
// Per player beyond the shipped two: player_sim_state, player_respawn, morph and component event data, and the
// send_morph and send_component objects.
constexpr uint32_t kPerPlayerOther = 0x16B4 + 0x4C + 0x420 + 0x1FC + 2 * 0x14;
constexpr uint32_t kMargin = 1u << 20;

}  // namespace coop_allocators_size

// Size of the `coop_allocators` pool once co-op uses the N=8 instances for `maxPlayers` (3..8) players, given the
// shipped size `originalSize`. Returns 0 outside that range or when the result does not fit 32 bits.
inline uint32_t coopAllocatorsPoolSize(uint32_t originalSize, uint32_t maxPlayers) {
    using namespace coop_allocators_size;
    if (maxPlayers <= sr2::kCoopDefaultPlayers || maxPlayers > sr2::kMaxReplicatedPlayers) return 0;
    const int64_t shippedPlayers = int64_t{kHistoryDepth} * sr2::kCoopDefaultPlayers * kPlayerN1Element;
    const int64_t size = int64_t{originalSize} - static_cast<int64_t>(fixedBytes(false)) - shippedPlayers +
                         static_cast<int64_t>(fixedBytes(true)) +
                         int64_t{kHistoryDepth} * maxPlayers * kPlayerN8Element +
                         int64_t{maxPlayers - sr2::kCoopDefaultPlayers} * kPerPlayerOther + kMargin;
    return size > UINT32_MAX ? 0 : static_cast<uint32_t>(size);
}
