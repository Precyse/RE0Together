// Milestone 5i, section 12 (no game): what a group create takes from the pools, the fit rule, the partial group rule for the
// trace, the log rate limit, and the group create entry as data.
#include <algorithm>
#include <cstdio>

#include "../src/partner_sites.h"
#include "../src/player_cap_patches.h"
#include "../src/rate_limit.h"
#include "../src/spawn_budget_rules.h"
#include "juiced_targets.h"

namespace {

constexpr uintptr_t kThunk = 0x10002000;
constexpr uintptr_t kJuicedMargin = 16;
constexpr uintptr_t kNearbyMargin = 0x500;  // no milestone 4 site closer to the group create
constexpr uint32_t kFourPlayers = 4;

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_failures;
}

const CodePatch* patchAt(const std::vector<CodePatch>& patches, uintptr_t address) {
    for (const CodePatch& patch : patches) {
        if (patch.address == address) return &patch;
    }
    return nullptr;
}

}  // namespace

int main() {
    using spawn_budget_rules::GroupEntry;
    using spawn_budget_rules::Need;

    const Need need = spawn_budget_rules::needFor({{3, false}, {0, true}, {2, true}});
    check(need.groups == 2 && need.elements == 6, "an entry without elements takes nothing; the others a group, their elements and the extra one");
    check(spawn_budget_rules::needFor({}).groups == 0 && spawn_budget_rules::needFor({}).elements == 0, "an empty set takes nothing");
    check(spawn_budget_rules::fits(need, 2, 6) && spawn_budget_rules::fits(need, 5, 100), "a create the pools can fill runs");
    check(!spawn_budget_rules::fits(need, 2, 5), "one element short: refused, before any group is linked");
    check(!spawn_budget_rules::fits(need, 1, 100), "one group short: refused");
    check(spawn_budget_rules::fits({}, 0, 0), "a create that takes nothing always runs");

    check(!spawn_budget_rules::isPartial({0x1000, {0x10, 0x20}}) && spawn_budget_rules::isPartial({0x1000, {0x10, 0}}),
          "a group with a null slot is partial");
    const std::vector<spawn_budget_rules::Group> groups = {{0x1000, {1, 2}}, {0x2000, {0}}, {0x3000, {5}}};
    check(spawn_budget_rules::partialGroups(groups) == std::vector<uintptr_t>{0x2000}, "the trace names the partial groups");

    rate_limit::RateLimit limit;
    uint32_t triggers = 0;
    check(limit.allow(5000, triggers) && triggers == 1, "the first trigger is logged, standing for one");
    check(!limit.allow(5999, triggers) && limit.allow(6000, triggers) && triggers == 2, "then once per second, counting the rest");

    const player_cap_patches::Thunks thunks{kThunk, kThunk, kThunk, kThunk, kThunk, kThunk, kThunk};
    const auto patches = player_cap_patches::forPlayers(kFourPlayers, thunks);
    const CodePatch* create = patchAt(patches, 0x910370);
    check(create && create->expected == std::vector<uint8_t>{0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8} && create->replacement.empty(),
          "55 8B EC 83 E4 F8 at 0x910370 is checked, the hook writes it");
    check(std::none_of(patches.begin(), patches.end(),
                       [](const CodePatch& p) { return p.address == 0x9140C0 || p.address == 0x914900 || p.address == 0x912FD0 || p.address == 0x90D44B; }),
          "the lock entries (S2 to S4) and the null guard (C1) are gone");
    check(player_cap_patches::forPlayers(2, thunks).empty(), "max_players 2 patches nothing");

    const uintptr_t low = create->address - kJuicedMargin;
    const uintptr_t high = create->address + create->expected.size() + kJuicedMargin;
    check(std::none_of(std::begin(juiced::kTargets), std::end(juiced::kTargets), [&](uint32_t target) { return target >= low && target < high; }),
          "the entry is not within 16 bytes of an address Juiced touches");
    check(std::all_of(std::begin(partner_sites::kSites), std::end(partner_sites::kSites),
                      [&](const partner_sites::Site& site) {
                          const uintptr_t distance = site.address > create->address ? site.address - create->address : create->address - site.address;
                          return distance >= kNearbyMargin;
                      }),
          "no milestone 4 site within 0x500 bytes of the entry");
    return g_failures == 0 ? 0 : 1;
}
