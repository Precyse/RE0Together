// scaledPoolSize and coopAllocatorsPoolSize: how the co-op pools grow with max_players (no game).
#include <cstdio>

#include "../src/pool_size.h"

namespace {

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_failures;
}

}  // namespace

int main() {
    constexpr uint32_t kCpu = 0x228000;
    constexpr uint32_t kGpu = 0x1071000;
    check(scaledPoolSize(kCpu, 2) == kCpu, "2 players keep the shipped size");
    check(scaledPoolSize(kCpu, 4) == kCpu * 3, "4 players = 3 guests");
    check(scaledPoolSize(kGpu, 4) == kGpu * 3, "gpu pool, 4 players");
    check(scaledPoolSize(kGpu, 12) == kGpu * 11, "12 players = 11 guests");
    check(scaledPoolSize(kCpu, 1) == 0, "no guests gives 0");
    check(scaledPoolSize(0x80000000u, 3) == 0, "a size over 32 bits gives 0");

    using namespace coop_allocators_size;
    constexpr uint32_t kShipped = 0x5ADBB0;
    constexpr uint32_t kMeasured4Players = 13225024 + (1u << 20);
    check(fixedBytes(false) == 2075760, "N=1 fixed bundles: 2,075,760 bytes");
    check(fixedBytes(true) == 9207000, "N=8 fixed bundles: 9,207,000 bytes");
    check(kPerPlayerOther == 7492, "other per-player objects: 7,492 bytes");
    check(coopAllocatorsPoolSize(kShipped, 4) == kMeasured4Players, "coop_allocators for 4 players: 13,225,024 + 1 MiB");
    check(coopAllocatorsPoolSize(kShipped, 3) < coopAllocatorsPoolSize(kShipped, 4), "3 players need less than 4");
    check(coopAllocatorsPoolSize(kShipped, 8) == kMeasured4Players + 4 * (15 * kPlayerN8Element + kPerPlayerOther),
          "each player adds 15 N=8 player elements and the other objects");
    check(coopAllocatorsPoolSize(kShipped, 2) == 0 && coopAllocatorsPoolSize(kShipped, 9) == 0,
          "2 players (shipped) and 9 players (past N=8) give 0");
    check(coopAllocatorsPoolSize(0xFFFFFFF0u, 4) == 0, "a size over 32 bits gives 0");
    return g_failures == 0 ? 0 : 1;
}
