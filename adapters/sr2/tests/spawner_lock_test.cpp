// Milestone 5i, section 9 (no game): the sweep rules, the log rate limit, and the group create site as data.
#include <algorithm>
#include <cstdio>

#include "../src/partner_sites.h"
#include "../src/player_cap_patches.h"
#include "../src/rate_limit.h"
#include "../src/spawner_rules.h"
#include "juiced_targets.h"

namespace {

constexpr uintptr_t kThunk = 0x10002000;
constexpr uintptr_t kJuicedMargin = 16;
constexpr uintptr_t kNearbyMargin = 0x2200;  // no milestone 4 site closer to the group create (K1 at 0x90E0BB is 0x22B5)
constexpr uintptr_t kGroupCreate = 0x910370;
constexpr uint32_t kFourPlayers = 4;

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_failures;
}

}  // namespace

int main() {
    using spawner_rules::Group;

    check(!spawner_rules::isPartial({0x1000, {0x10, 0x20, 0x30}}), "a group whose slots are all filled is complete");
    check(spawner_rules::isPartial({0x1000, {0x10, 0, 0x30}}) && spawner_rules::isPartial({0x1000, {0}}),
          "a group with a null slot is partial, also the first slot");
    check(!spawner_rules::isPartial({0x1000, {}}), "a template without slots has nothing missing");
    const std::vector<Group> groups = {{0x1000, {1, 2}}, {0x2000, {1, 0}}, {0x3000, {0, 0}}, {0x4000, {5}}};
    check(spawner_rules::partialGroups(groups) == std::vector<uintptr_t>{0x2000, 0x3000}, "the sweep names the partial groups, in order");
    check(spawner_rules::partialGroups({}).empty(), "an empty list sweeps nothing");
    check(spawner_rules::mayFree(1) && !spawner_rules::mayFree(2) && !spawner_rules::mayFree(3),
          "only a create that took the lock just now sweeps, not one inside the tick that held it");

    rate_limit::RateLimit limit;
    uint32_t triggers = 0;
    check(limit.allow(5000, triggers) && triggers == 1, "the first trigger is logged, standing for one");
    check(!limit.allow(5100, triggers) && !limit.allow(5999, triggers), "triggers within a second are only counted");
    check(limit.allow(6000, triggers) && triggers == 3, "the next line comes a second later and says how many it stands for");
    check(!limit.allow(6500, triggers) && limit.allow(7000, triggers) && triggers == 2, "and so on, once per second");

    const player_cap_patches::Thunks thunks{kThunk, kThunk, kThunk, kThunk, kThunk, kThunk, kThunk};
    const auto patches = player_cap_patches::forPlayers(kFourPlayers, thunks);
    const auto create = std::find_if(patches.begin(), patches.end(), [](const CodePatch& p) { return p.address == kGroupCreate; });
    check(create != patches.end() && create->expected == std::vector<uint8_t>{0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8} &&
              create->replacement.empty(),
          "S1: 55 8B EC 83 E4 F8 at 0x910370 is checked, the hook writes it");
    check(std::none_of(patches.begin(), patches.end(), [](const CodePatch& p) { return p.address == 0x90D44B; }),
          "C1 (0x90D44B) is gone");
    check(player_cap_patches::forPlayers(2, thunks).empty(), "max_players 2 patches nothing");

    const uintptr_t low = kGroupCreate - kJuicedMargin;
    const uintptr_t high = kGroupCreate + 6 + kJuicedMargin;
    check(std::none_of(std::begin(juiced::kTargets), std::end(juiced::kTargets),
                       [&](uint32_t target) { return target >= low && target < high; }),
          "S1 is not within 16 bytes of an address Juiced touches");
    check(std::all_of(partner_sites::kSites, partner_sites::kSites + partner_sites::kSiteCount, [](const partner_sites::Site& site) {
              const uintptr_t distance = site.address > kGroupCreate ? site.address - kGroupCreate : kGroupCreate - site.address;
              return distance >= kNearbyMargin;
          }),
          "no milestone 4 site within 0x2200 bytes of the group create");
    return g_failures == 0 ? 0 : 1;
}
