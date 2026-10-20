// The milestone 17 wave 3 Zombie Uprising site tables (no game): the 24 rows, their shapes, the callee of each retargeted or replaced call,
// where each replaced block jumps back to, the loop bounds, the sets, and clearance from every other site and from everything Juiced touches.
#include <algorithm>
#include <cstdio>
#include <set>
#include <span>
#include <string_view>
#include <vector>

#include "../src/anim_stream_sites.h"
#include "../src/char_types_sites.h"
#include "../src/hud_sites.h"
#include "../src/multiplayer_sites.h"
#include "../src/net_handlers_sites.h"
#include "../src/outfit_sync_sites.h"
#include "../src/partner_replacement.h"
#include "../src/party_ai_sites.h"
#include "../src/revive_sites.h"
#include "../src/session_kick_sites.h"
#include "../src/sr2_addresses.h"
#include "../src/store_sites.h"
#include "../src/vehicle_pool_references.h"
#include "../src/vehicle_pool_sites.h"
#include "../src/vehicle_seating_sites.h"
#include "../src/zombie_uprising_rules.h"
#include "../src/zombie_uprising_sites.h"
#include "check.h"
#include "juiced_targets.h"
#include "other_sites.h"
#include "site_lookup.h"

namespace {

using partner_sites::Kind;
using partner_sites::Site;
using partner_sites::Thunk;

constexpr uintptr_t kJuicedMargin = 16;
constexpr size_t kBranchBytes = 5;
constexpr size_t kDwordBytes = 4;
constexpr uintptr_t kThunk = 0x10004000;
constexpr uint8_t kCallOpcode = 0xE8;
constexpr uint8_t kJumpRel32Opcode = 0xE9;
constexpr uint8_t kJumpRel8Opcode = 0xEB;
constexpr size_t kWrittenSites = 11;
constexpr size_t kCheckRows = 13;
constexpr size_t kCallAtStart = 0;
constexpr size_t kRowsOfSets[] = {9, 3, 3, 1, 8};
constexpr uintptr_t kPartnerFractionCallOffset = 2;  // ZU3 starts with `mov ecx, eax`
constexpr uintptr_t kFractionWorker = 0x4067B0;

std::vector<Site> ourSites() {
    std::vector<Site> all;
    for (const zombie_uprising_sites::Set& set : zombie_uprising_sites::kSets) all.insert(all.end(), set.sites.begin(), set.sites.end());
    return all;
}

size_t sizeOf(std::string_view hex) { return partner_sites::parseHex(hex).size(); }

size_t sizeOf(const Site& site) { return sizeOf(site.expected); }

bool isWritten(const Site& site) { return site.thunk != Thunk::None || !site.tail.empty(); }

// Where the `call rel32` that starts at `offset` into the site's expected bytes goes.
uintptr_t callTarget(const Site& site, size_t offset) {
    const auto bytes = partner_sites::parseHex(site.expected);
    const uint32_t displacement = bytes[offset + 1] | bytes[offset + 2] << 8 | bytes[offset + 3] << 16 | static_cast<uint32_t>(bytes[offset + 4]) << 24;
    return site.address + offset + kBranchBytes + displacement;
}

// Where the jump at the start of a call block's tail goes (rel8 or rel32).
uintptr_t tailJumpTarget(const Site& site) {
    const auto tail = partner_sites::parseHex(site.tail);
    const uintptr_t jumpAt = site.address + kBranchBytes;
    if (tail[0] == kJumpRel8Opcode) return jumpAt + 2 + static_cast<int8_t>(tail[1]);
    const uint32_t displacement = tail[1] | tail[2] << 8 | tail[3] << 16 | static_cast<uint32_t>(tail[4]) << 24;
    return jumpAt + kBranchBytes + displacement;
}

// Every site table of the other milestones, by range.
std::vector<other_sites::Range> earlierRanges() {
    std::vector<other_sites::Range> others = other_sites::milestones3to8();
    for (const auto& more : {other_sites::activities(), other_sites::players3and4()}) others.insert(others.end(), more.begin(), more.end());
    const auto add = [&others](std::span<const Site> table) {
        for (const Site& site : table) others.push_back({site.address, sizeOf(site)});
    };
    for (const auto table : multiplayer_sites::kScriptTraces) add(table);
    for (const player_sites::SkinPoolSite& site : player_sites::kSkinPool) others.push_back({site.address, sizeOf(site.expected)});
    add(player_sites::kCutsceneVisuals);
    for (const auto& row : vehicle_pool_references::kReferences) others.push_back({row.site, kDwordBytes});
    for (const auto& bound : vehicle_pool_sites::kBounds) others.push_back({bound.address, sizeOf(bound.expected)});
    add(vehicle_pool_sites::kHeadroomBlocks);
    for (const auto table : {std::span<const Site>(outfit_sync_sites::kLookSync), std::span<const Site>(outfit_sync_sites::kItemWear),
                             std::span<const Site>(outfit_sync_sites::kWeaponSlots), std::span<const Site>(outfit_sync_sites::kTraces)}) {
        add(table);
    }
    for (const session_kick_sites::Set& set : session_kick_sites::kSets) add(set.sites);
    for (const anim_stream_sites::Phase& phase : anim_stream_sites::kPhases) {
        add(phase.sites);
        for (const anim_stream_sites::NeedTableSite& site : phase.immediates) others.push_back({site.address, sizeOf(site.expected)});
    }
    for (const char_types_sites::Row& row : char_types_sites::kRows) others.push_back({row.address, sizeOf(row.expected)});
    for (const hud_sites::Set& set : hud_sites::kSets) add(set.sites);
    for (const net_handlers_sites::Set& set : net_handlers_sites::kSets) add(set.sites);
    for (const party_ai_sites::Phase& phase : party_ai_sites::kPhases) add(phase.sites);
    for (const store_sites::Set& set : store_sites::kSets) add(set.sites);
    for (const auto table : {std::span<const Site>(revive_sites::kRevive), std::span<const Site>(revive_sites::kNotoriety),
                             std::span<const Site>(vehicle_seating_sites::kTrafficking), std::span<const Site>(vehicle_seating_sites::kCheckpoint),
                             std::span<const Site>(vehicle_seating_sites::kMissionExit)}) {
        add(table);
    }
    return others;
}

// The ids of every site table of the other milestones.
std::set<std::string_view> otherIds() {
    std::set<std::string_view> ids;
    const auto add = [&ids](std::span<const Site> table) {
        for (const Site& site : table) ids.insert(site.id);
    };
    add(partner_sites::kSites);
    for (const multiplayer_sites::Phase& phase : multiplayer_sites::kPhases) add(phase.sites);
    for (const auto table : multiplayer_sites::kScriptTraces) add(table);
    add(activity_sites::kPhase1);
    add(activity_sites::kPhase2);
    add(activity_sites::kPhase3);
    for (const diversion_sites::Set& set : diversion_sites::kSets) add(set.sites);
    for (const store_sites::Set& set : store_sites::kSets) add(set.sites);
    for (const hud_sites::Set& set : hud_sites::kSets) add(set.sites);
    for (const net_handlers_sites::Set& set : net_handlers_sites::kSets) add(set.sites);
    for (const party_ai_sites::Phase& phase : party_ai_sites::kPhases) add(phase.sites);
    for (const session_kick_sites::Set& set : session_kick_sites::kSets) add(set.sites);
    for (const anim_stream_sites::Phase& phase : anim_stream_sites::kPhases) add(phase.sites);
    return ids;
}

void theTables() {
    const std::vector<Site> sites = ourSites();
    const std::set<std::string_view> others = otherIds();
    std::set<std::string_view> ids;
    std::set<uintptr_t> addresses;
    bool unique = true;
    for (const Site& site : sites) unique = unique && ids.insert(site.id).second && addresses.insert(site.address).second && others.count(site.id) == 0;
    check(unique, "ids and addresses are unique, and no id is used by another milestone");
    const auto written = static_cast<size_t>(std::count_if(sites.begin(), sites.end(), isWritten));
    check(written == kWrittenSites && sites.size() == kWrittenSites + kCheckRows, "11 written sites and 13 check rows");
    bool setSizes = std::size(zombie_uprising_sites::kSets) == std::size(kRowsOfSets);
    for (size_t i = 0; setSizes && i < std::size(kRowsOfSets); ++i) setSizes = zombie_uprising_sites::kSets[i].sites.size() == kRowsOfSets[i];
    check(setSizes, "five sets of 9, 3, 3, 1 and 8 rows");
    const std::string_view names[] = {"start and end", "wave bonus", "trigger zones", "waiting prompt", "spawn tests"};
    bool named = true;
    for (size_t i = 0; i < std::size(names); ++i) named = named && names[i] == zombie_uprising_sites::kSets[i].name;
    check(named, "the sets are named start and end, wave bonus, trigger zones, waiting prompt and spawn tests");
    check(std::all_of(sites.begin(), sites.end(), [](const Site& site) { return site.kind != Kind::Bytes || site.thunk == Thunk::None; }),
          "a bytes row has no thunk");
}

void theCalls() {
    const std::vector<Site> sites = ourSites();
    struct Call {
        const char* id;
        size_t offset;
        uintptr_t callee;
    };
    bool callees = true;
    for (const Call call : {Call{"ZU1", kCallAtStart, sr2::kRemotePlayerGetter}, Call{"ZU2", kCallAtStart, sr2::kRemotePlayerGetter},
                            Call{"ZU3", kPartnerFractionCallOffset, kFractionWorker}, Call{"ZU4", kCallAtStart, sr2::kPointInBox},
                            Call{"ZU5", kCallAtStart, sr2::kPointInBox}, Call{"ZU6", kCallAtStart, sr2::kPartnerMemberGetter}}) {
        const Site* site = siteNamed(sites, call.id);
        const auto bytes = partner_sites::parseHex(site->expected);
        callees = callees && bytes[call.offset] == kCallOpcode && callTarget(*site, call.offset) == call.callee;
    }
    check(callees, "ZU1 and ZU2 call 0x9D7BD0, ZU3 calls 0x4067B0, ZU4 and ZU5 call 0xBE9890, ZU6 calls 0x8CA380");
    bool retargets = true;
    for (const char* id : {"ZU4", "ZU5"}) {
        const Site* site = siteNamed(sites, id);
        const auto written = partner_replacement::replacementFor(*site, kThunk);
        retargets = retargets && site->kind == Kind::RetargetCall && sizeOf(*site) == kBranchBytes && written.size() == kBranchBytes && written[0] == kCallOpcode;
    }
    check(retargets, "ZU4 and ZU5 are 5 byte call retargets that keep their opcode and length");
    check(siteNamed(sites, "ZU4")->thunk == Thunk::AnyRemoteInVehicleBox && siteNamed(sites, "ZU5")->thunk == Thunk::AnyRemoteInVehicleBox,
          "the zones use the garage box thunk of GB1");
    check(siteNamed(sites, "ZU6")->thunk == Thunk::AskedMemberNames && siteNamed(sites, "ZU6")->tail == "90 90 90 90 90",
          "the waiting prompt uses the name list thunk of MF17 with the same five nops");
}

void theBlocks() {
    const std::vector<Site> sites = ourSites();
    struct Block {
        const char* id;
        const char* resumeRow;
        uintptr_t resume;
    };
    bool shapes = true;
    bool fits = true;
    bool jumps = true;
    for (const Block block : {Block{"ZU1", "ZUc1", 0x5BA968}, Block{"ZU2", "ZUc2", 0x5BAE3E}, Block{"ZU3", "ZUc3", 0x5B9C3B}, Block{"ZS1", "ZSc1", 0x5B940E}}) {
        const Site* site = siteNamed(sites, block.id);
        const Site* row = siteNamed(sites, block.resumeRow);
        shapes = shapes && site && row && site->kind == Kind::CallBlock && row->kind == Kind::Bytes && row->address == block.resume;
        const auto written = partner_replacement::replacementFor(*site, kThunk);
        fits = fits && written.size() <= sizeOf(*site) && written[0] == kCallOpcode;
        const auto tail = partner_sites::parseHex(site->tail);
        jumps = jumps && (tail[0] == kJumpRel8Opcode || tail[0] == kJumpRel32Opcode) && tailJumpTarget(*site) == block.resume;
    }
    check(shapes, "ZU1, ZU2, ZU3 and ZS1 are call blocks, each with a check row at the address it resumes at");
    check(fits, "each call block is a call to the thunk and its tail, no longer than the bytes it replaces");
    check(jumps, "the tail of each call block jumps to its check row: ZU1 0x5BA968, ZU2 0x5BAE3E, ZU3 0x5B9C3B, ZS1 0x5B940E");

    bool exact = true;
    for (const char* id : {"ZU1", "ZU6", "ZS2", "ZS4"}) {
        const Site* site = siteNamed(sites, id);
        exact = exact && partner_replacement::replacementFor(*site, kThunk).size() == sizeOf(*site);
    }
    check(exact, "ZU1, ZU6, ZS2 and ZS4 replace every byte of their block, none is left over");
}

void theLoopBounds() {
    const std::vector<Site> sites = ourSites();
    bool bounds = true;
    for (const char* id : {"ZS3", "ZS5"}) {
        const Site* site = siteNamed(sites, id);
        const auto expected = partner_sites::parseHex(site->expected);
        const auto tail = partner_sites::parseHex(site->tail);
        bounds = bounds && site->kind == Kind::Bytes && tail.size() == 3 && tail[0] == expected[0] && tail[1] == expected[1] &&
                 expected[2] == zombie_uprising_rules::kSpawnPairSlots && tail[2] == zombie_uprising_rules::kSpawnPlayerSlots;
    }
    check(bounds, "ZS3 and ZS5 raise the loop bound from 2 to the 12 player slots and change nothing else");
    check(siteNamed(sites, "ZS2")->thunk == Thunk::ZombieSpawnPlayerAtEax && siteNamed(sites, "ZS4")->thunk == Thunk::ZombieSpawnPlayerAtEcx &&
              siteNamed(sites, "ZS1")->thunk == Thunk::ZombieSpawnPlayers,
          "ZS1, ZS2 and ZS4 have the spawn player stubs");
}

void theClearance() {
    const std::vector<Site> sites = ourSites();
    bool apart = true;
    for (const Site& a : sites) {
        for (const Site& b : sites) {
            if (&a != &b && other_sites::overlaps(a.address, sizeOf(a), b.address, sizeOf(b))) apart = false;
        }
    }
    check(apart, "no two sites overlap");

    const std::vector<other_sites::Range> others = earlierRanges();
    bool clear = true;
    bool clearOfJuiced = true;
    for (const Site& site : sites) {
        if (other_sites::anyOverlap(others, site.address, sizeOf(site))) {
            std::printf("     %s at 0x%X overlaps a site of an earlier milestone\n", site.id, static_cast<unsigned>(site.address));
            clear = false;
        }
        const uintptr_t first = site.address - kJuicedMargin;
        const uintptr_t last = site.address + sizeOf(site) + kJuicedMargin;
        if (std::any_of(std::begin(juiced::kTargets), std::end(juiced::kTargets), [&](uint32_t target) { return target >= first && target < last; })) {
            std::printf("     %s at 0x%X is within %u bytes of a Juiced target\n", site.id, static_cast<unsigned>(site.address),
                        static_cast<unsigned>(kJuicedMargin));
            clearOfJuiced = false;
        }
    }
    check(clear, "no site overlaps a site of an earlier milestone, wave 1 and wave 2 included");
    check(clearOfJuiced, "no site is within 16 bytes of a Juiced target");
}

}  // namespace

int main() {
    theTables();
    theCalls();
    theBlocks();
    theLoopBounds();
    theClearance();
    return g_failures == 0 ? 0 : 1;
}
