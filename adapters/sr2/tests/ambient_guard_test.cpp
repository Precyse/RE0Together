// Milestone 5i, C1 (no game): the rate limit of the guard's log, and the site as data.
#include <algorithm>
#include <cstdio>

#include "../src/ambient_guard_rules.h"
#include "../src/partner_sites.h"
#include "../src/player_cap_patches.h"
#include "juiced_targets.h"

namespace {

constexpr uintptr_t kThunk = 0x10002000;
constexpr uintptr_t kSkipThunk = 0x10009000;
constexpr uintptr_t kJuicedMargin = 16;
constexpr uint32_t kFourPlayers = 4;
constexpr uintptr_t kElementLoad = 0x90D44B;

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_failures;
}

}  // namespace

int main() {
    ambient_guard_rules::RateLimit limit;
    uint32_t skipped = 0;
    check(limit.allow(5000, skipped) && skipped == 1, "the first skip is logged, standing for one");
    check(!limit.allow(5100, skipped) && !limit.allow(5999, skipped), "skips within a second are only counted");
    check(limit.allow(6000, skipped) && skipped == 3, "the next line comes a second later and says how many it stands for");
    check(!limit.allow(6500, skipped) && limit.allow(7000, skipped) && skipped == 2, "and so on, once per second");

    const player_cap_patches::Thunks thunks{kThunk, kThunk, kThunk, kThunk, kThunk, kSkipThunk};
    const auto patches = player_cap_patches::forPlayers(kFourPlayers, thunks);
    const CodePatch* guard = nullptr;
    for (const CodePatch& patch : patches) {
        if (patch.address == kElementLoad) guard = &patch;
    }
    const uint32_t displacement = static_cast<uint32_t>(kSkipThunk - (kElementLoad + 5));
    check(guard && guard->expected == std::vector<uint8_t>{0x8B, 0x75, 0x00, 0x8B, 0x4E, 0x34} &&
              guard->replacement == std::vector<uint8_t>{0xE9, static_cast<uint8_t>(displacement),
                                                        static_cast<uint8_t>(displacement >> 8),
                                                        static_cast<uint8_t>(displacement >> 16),
                                                        static_cast<uint8_t>(displacement >> 24), 0x90},
          "C1: 8B 75 00 8B 4E 34 at 0x90D44B is checked and becomes jmp rel32 to the thunk and a nop");
    check(player_cap_patches::forPlayers(2, thunks).empty(), "max_players 2 patches nothing");

    const uintptr_t low = kElementLoad - kJuicedMargin;
    const uintptr_t high = kElementLoad + 6 + kJuicedMargin;
    check(std::none_of(std::begin(juiced::kTargets), std::end(juiced::kTargets),
                       [&](uint32_t target) { return target >= low && target < high; }),
          "C1 is not within 16 bytes of an address Juiced touches");
    bool clear = true;
    for (const partner_sites::Site& site : partner_sites::kSites) {
        const uintptr_t size = partner_sites::parseHex(site.expected).size();
        clear = clear && !(site.address < kElementLoad + 6 && kElementLoad < site.address + size);
    }
    check(clear, "C1 overlaps no milestone 4 site");
    return g_failures == 0 ? 0 : 1;
}
