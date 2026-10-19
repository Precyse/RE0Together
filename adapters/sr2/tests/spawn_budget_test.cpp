// Milestone 5i, section 12 and milestone 11 B6 (no game): what a group create takes from the pools, the fit rule, the partial
// group rule for the trace, the log rate limit, the census (the free list walk, the counts per caller, the line), and the
// group create entry as data.
#include <algorithm>
#include <cstdio>
#include <map>
#include <string_view>

#include "../src/partner_sites.h"
#include "../src/player_cap_patches.h"
#include "../src/rate_limit.h"
#include "../src/spawn_budget_rules.h"
#include "../src/trace_sites.h"
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

    spawn_budget_rules::CallerCounts<2> setups({0x6D036C, 0x9CDF5E});
    setups.record(0x6D036C, false);
    setups.record(0x6D036C, true);
    setups.record(0x123456, true);
    check(setups.at(0).calls == 2 && setups.at(0).failed == 1 && setups.at(1).calls == 0,
          "NPC setups and the ones with no human are counted per caller; a caller that is not listed is not counted");
    check(setups.describe() == "0x6D036C 2/1 0x9CDF5E 0/0", "the NPC setup counts as the census line shows them");

    using spawn_budget_rules::Caller;
    using spawn_budget_rules::Outcome;
    using spawn_budget_rules::Verdict;
    check(spawn_budget_rules::callerOf(0x910B58) == Caller::Ambient && spawn_budget_rules::callerOf(0x912E4C) == Caller::Crew &&
              spawn_budget_rules::callerOf(0x9163C5) == Caller::Shop && spawn_budget_rules::callerOf(0x123456) == Caller::OtherReserved,
          "the group create callers: the child spawn, crews, shops and any other address");
    check(spawn_budget_rules::outcomeOf(Verdict::Create, true) == Outcome::Made && spawn_budget_rules::outcomeOf(Verdict::Create, false) == Outcome::Failed &&
              spawn_budget_rules::outcomeOf(Verdict::Reserve, false) == Outcome::Reserve &&
              spawn_budget_rules::outcomeOf(Verdict::NoRoom, false) == Outcome::NoRoom &&
              spawn_budget_rules::outcomeOf(Verdict::Backoff, false) == Outcome::Backoff,
          "a create that ran is made or failed by the answer of the game; a refusal keeps its kind");

    spawn_budget_rules::GroupCreateCounts groups;
    groups.record(Caller::Ambient, Outcome::Made);
    groups.record(Caller::Ambient, Outcome::Made);
    groups.record(Caller::Ambient, Outcome::Reserve);
    groups.record(Caller::Ambient, Outcome::Backoff);
    groups.record(Caller::Ambient, Outcome::Backoff);
    groups.record(Caller::Ambient, Outcome::Backoff);
    groups.record(Caller::Ambient, Outcome::NoRoom);
    groups.record(Caller::Ambient, Outcome::Failed);
    groups.record(Caller::Crew, Outcome::Made);
    groups.record(Caller::Crew, Outcome::NoRoom);
    groups.record(Caller::Shop, Outcome::Failed);
    groups.record(Caller::OtherReserved, Outcome::Made);
    check(groups.at(Caller::Ambient).made == 2 && groups.at(Caller::Ambient).backoff == 3 && groups.at(Caller::Crew).noRoom == 1,
          "group creates are counted per caller and outcome");
    check(groups.describe() ==
              "0x910B58 ambient made 2 reserve hits 1 back-off 3 no room 1 failed 1, 0x912E4C reserved made 1 no room 1 failed 0, "
              "0x9163C5 reserved made 0 no room 0 failed 1, other reserved made 1 no room 0 failed 0",
          "the group create counts as the census line shows them");

    spawn_budget_rules::TakenByCaller<4> nodes({0x914820, 0xA25035, 0xA7E8B5, 0xA815C8});
    nodes.add(0x914820, 3, 7);
    nodes.add(0x914820, 1, 1);
    nodes.add(0xA7E8B5, 5, 9);
    nodes.add(0x123456, 100, 100);
    check(nodes.describe() == "0x914820 4/8 0xA25035 0/0 0xA7E8B5 5/9 0xA815C8 0/0",
          "the chunk node loader groups and elements add up per caller; a caller that is not listed is not counted");

    vehicle_trace_rules::Totals vehicles;
    vehicles.record(false, vehicle_trace_rules::Failure::Pool);
    vehicles.record(true, vehicle_trace_rules::Failure::Other);
    const spawn_budget_rules::Census census{12, 2399, 2388, 1, 0, groups.describe(), nodes.describe(), 20, 31, vehicles, setups.describe(), 2, 2, 250};
    check(spawn_budget_rules::censusLine(census) ==
              "trace: census: free groups 12/2400 (in use 2388), free elements 2399/2400 (in use 1), free npc humans 0/150, "
              "group creates " + groups.describe() + ", chunk nodes took groups/elements " + nodes.describe() +
              ", vehicles free 20/32 creation data 31/32, remote vehicle creates 2 failed 1 (pool 1, creation data 0, other 0), "
              "npc setups/without a human by caller 0x6D036C 2/1 0x9CDF5E 0/0, remote humans 2, remote members 2, player areas 2.50",
          "the census line");
    const spawn_budget_rules::Census none{0, 0, 0, 0, 0, "", "", 0, 0, {}, "", 0, 0, 0};
    check(spawn_budget_rules::censusLine(none).ends_with("player areas 0.00"), "no area evaluated reads 0.00");
}

// The ambient reserve and back-off (milestone 12): what a create may do for the free counts a bounded walk found.
void reserveRules() {
    using spawn_budget_rules::decide;
    using spawn_budget_rules::Need;
    using spawn_budget_rules::Verdict;
    constexpr uint32_t kGroups = spawn_budget_rules::kAmbientReserveGroups;
    constexpr uint32_t kElements = spawn_budget_rules::kAmbientReserveElements;
    const Need need{2, 6};

    check(kGroups == 480 && kElements == 480 && spawn_budget_rules::kAmbientRetryMs == 1000, "the reserve is 480 and 480, the wait 1000 ms");
    check(decide(true, need, need.groups + kGroups, need.elements + kElements, false) == Verdict::Create,
          "ambient: created at exactly need + reserve");
    check(decide(true, need, need.groups + kGroups - 1, need.elements + kElements, false) == Verdict::Reserve,
          "ambient: one group short of the reserve is refused as a reserve hit");
    check(decide(true, need, need.groups + kGroups, need.elements + kElements - 1, false) == Verdict::Reserve,
          "ambient: one element short of the reserve is refused as a reserve hit");
    check(decide(true, need, need.groups, need.elements, false) == Verdict::Reserve, "ambient: room for the need alone is a reserve hit");
    check(decide(true, need, need.groups - 1, need.elements + kElements, false) == Verdict::NoRoom &&
              decide(true, need, need.groups + kGroups, need.elements - 1, false) == Verdict::NoRoom,
          "ambient: room for less than the need is no room at all");
    check(decide(false, need, need.groups, need.elements, false) == Verdict::Create, "reserved: created down to exactly the need");
    check(decide(false, need, need.groups, need.elements - 1, false) == Verdict::NoRoom &&
              decide(false, need, need.groups - 1, need.elements, false) == Verdict::NoRoom,
          "reserved: one short of the need is no room");
    check(decide(false, need, need.groups, need.elements, true) == Verdict::Create && decide(false, {}, 0, 0, false) == Verdict::Create,
          "reserved: the wait never applies to it, and a create that takes nothing runs");
    check(decide(true, need, 5000, 5000, true) == Verdict::Backoff, "ambient: during the wait nothing is created, whatever room there is");
    check(decide(true, need, 5000, 5000, false) == Verdict::Create, "ambient: plenty of room outside the wait");

    check(spawn_budget_rules::walkLimit(true, 7, kGroups) == 7 + kGroups && spawn_budget_rules::walkLimit(false, 7, kGroups) == 7,
          "the walk goes to need + reserve for the ambient caller, to the need for the others");
    check(spawn_budget_rules::isAmbientCaller(0x910B58) && !spawn_budget_rules::isAmbientCaller(0x912E4C) &&
              !spawn_budget_rules::isAmbientCaller(0x9163C5) && !spawn_budget_rules::isAmbientCaller(0x123456) && !spawn_budget_rules::isAmbientCaller(0),
          "only the child spawn is ambient; crews, shops and any unknown caller are reserved");

    check(spawn_budget_rules::startsBackoff(true, Verdict::Reserve) && spawn_budget_rules::startsBackoff(true, Verdict::NoRoom) &&
              !spawn_budget_rules::startsBackoff(true, Verdict::Create) && !spawn_budget_rules::startsBackoff(true, Verdict::Backoff) &&
              !spawn_budget_rules::startsBackoff(false, Verdict::NoRoom),
          "an ambient refusal starts the wait; the wait, a create and a reserved refusal do not");
    constexpr uint64_t kRefusedAt = 5000;
    const uint64_t until = kRefusedAt + spawn_budget_rules::kAmbientRetryMs;
    check(spawn_budget_rules::inBackoff(kRefusedAt, until) && spawn_budget_rules::inBackoff(until - 1, until) &&
              !spawn_budget_rules::inBackoff(until, until) && !spawn_budget_rules::inBackoff(0, 0),
          "the wait lasts until the deadline and not past it; before any refusal there is none");
}

}  // namespace

int main() {
    using spawn_budget_rules::GroupEntry;
    using spawn_budget_rules::Need;

    censusRules();
    reserveRules();

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
    const partner_sites::Site* chunkNodes = nullptr;
    const partner_sites::Site* remoteVehicle = nullptr;
    for (const partner_sites::Site& site : trace_sites::kCensus) {
        if (std::string_view(site.id) == "NC2") chunkNodes = &site;
        if (std::string_view(site.id) == "VT1") remoteVehicle = &site;
    }
    check(chunkNodes && chunkNodes->kind == partner_sites::Kind::HookEntry && chunkNodes->address == sr2::kChunkNodeLoad &&
              partner_sites::parseHex(chunkNodes->expected) == std::vector<uint8_t>{0x81, 0xEC, 0x4C, 0x02, 0x00, 0x00, 0xA1, 0x50, 0x0B, 0xE8},
          "NC2 hooks the chunk node loader 0x9140C0, 10 checked bytes");
    check(remoteVehicle && remoteVehicle->kind == partner_sites::Kind::PointerSwap && remoteVehicle->address == sr2::kVehicleNetCreateSlot &&
              partner_sites::parseHex(remoteVehicle->expected) == std::vector<uint8_t>{0x30, 0x3F, 0x87, 0x00},
          "VT1 swaps the vehicle class create slot 0xE2BA78 (the pointer to 0x873F30)");
    for (const partner_sites::Site* site : {chunkNodes, remoteVehicle}) {
        if (!site) continue;
        const uintptr_t first = site->address - kJuicedMargin;
        const uintptr_t last = site->address + partner_sites::parseHex(site->expected).size() + kJuicedMargin;
        check(std::none_of(std::begin(juiced::kTargets), std::end(juiced::kTargets), [&](uint32_t target) { return target >= first && target < last; }),
              "the trace row is not within 16 bytes of an address Juiced touches");
    }
    return g_failures == 0 ? 0 : 1;
}
