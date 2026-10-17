// Milestone 5i, section 12 and milestone 11 B6 (no game): what a group create takes from the pools, the fit rule, the partial
// group rule for the trace, the log rate limit, the census (the free list walk, the counts per caller, the line), and the
// group create entry as data.
#include <algorithm>
#include <cstdio>
#include <map>

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

// A made-up free list: node -> its link.
uint32_t countList(const std::map<uintptr_t, uintptr_t>& links, uintptr_t head, uint32_t limit) {
    return spawn_budget_rules::countCircular(head, limit, [&](uintptr_t node) {
        const auto link = links.find(node);
        return link == links.end() ? uintptr_t{0} : link->second;
    });
}

void censusRules() {
    const std::map<uintptr_t, uintptr_t> circle = {{0x100, 0x200}, {0x200, 0x300}, {0x300, 0x100}};
    check(countList(circle, 0x100, 150) == 3, "a circular free list is counted once round");
    check(countList(circle, 0x100, 2) == 2, "and capped at the limit");
    check(countList(circle, 0, 150) == 0, "an empty list (head 0) has no free node");
    check(countList({{0x100, 0}}, 0x100, 150) == 1, "a list that ends in a null link stops there");
    check(countList({{0x100, 0x200}, {0x200, 0x300}, {0x300, 0x200}}, 0x100, 5) == 5, "a list that never comes back to its head is capped");

    spawn_budget_rules::CallerCounts<3> groups({0x910B58, 0x912E4C, 0x9163C5});
    groups.record(0x910B58, false);
    groups.record(0x910B58, true);
    groups.record(0x9163C5, false);
    groups.record(0x123456, true);
    check(groups.at(0).calls == 2 && groups.at(0).failed == 1 && groups.at(1).calls == 0 && groups.at(2).calls == 1,
          "creates and refused creates are counted per caller; a caller that is not listed is not counted");
    check(groups.describe() == "0x910B58 2/1 0x912E4C 0/0 0x9163C5 1/0", "the counts as the census line shows them");

    const spawn_budget_rules::Census census{12, 2399, 0, groups.describe(), "0x6D036C 4/1", 2, 250};
    check(spawn_budget_rules::censusLine(census) ==
              "trace: census: free groups 12/2400, free elements 2399/2400, free npc humans 0/150, creates/refused by caller "
              "0x910B58 2/1 0x912E4C 0/0 0x9163C5 1/0, npc setups/without a human by caller 0x6D036C 4/1, remote humans 2, "
              "player areas 2.50",
          "the census line");
    const spawn_budget_rules::Census none{0, 0, 0, "", "", 0, 0};
    check(spawn_budget_rules::censusLine(none).ends_with("player areas 0.00"), "no area evaluated reads 0.00");
}

}  // namespace

int main() {
    using spawn_budget_rules::GroupEntry;
    using spawn_budget_rules::Need;

    censusRules();

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

    rate_limit::RateLimit minute(60000);
    check(minute.allow(1000, triggers) && !minute.allow(60999, triggers) && minute.allow(61000, triggers),
          "a limit with a longer interval writes once per interval");

    rate_limit::OncePerKey once;
    check(once.firstTime(7) && !once.firstTime(7) && once.firstTime(8), "a line per key is written once");
    for (uint64_t key = 100; key < 100 + rate_limit::kMaxOnceKeys; ++key) once.firstTime(key);
    check(once.firstTime(7), "a full set forgets its oldest key");

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
