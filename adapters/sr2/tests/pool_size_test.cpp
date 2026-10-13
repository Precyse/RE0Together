// scaledPoolSize: how the co-op player pools grow with max_players (no game).
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
    return g_failures == 0 ? 0 : 1;
}
