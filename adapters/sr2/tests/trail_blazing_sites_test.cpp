// The milestone 17 wave 3 Trail Blazing site tables (no game): ids and addresses, shapes, the callee of each retargeted call, the jump
// out blocks with their check rows and resume addresses, the sets, and clearance from every other site and from everything Juiced
// touches.
#include <algorithm>
#include <cstdio>
#include <set>
#include <span>
#include <string_view>
#include <vector>

#include "../src/anim_stream_sites.h"
#include "../src/char_types_sites.h"
#include "../src/hud_sites.h"
#include "../src/mission_flow_sites.h"
#include "../src/multiplayer_sites.h"
#include "../src/net_handlers_sites.h"
#include "../src/outfit_sync_sites.h"
#include "../src/partner_replacement.h"
#include "../src/party_ai_sites.h"
#include "../src/player_sites.h"
#include "../src/revive_sites.h"
#include "../src/session_kick_sites.h"
#include "../src/sr2_addresses.h"
#include "../src/store_sites.h"
#include "../src/trail_blazing_sites.h"
#include "../src/vehicle_pool_references.h"
#include "../src/vehicle_pool_sites.h"
#include "../src/vehicle_seating_sites.h"
#include "juiced_targets.h"
#include "other_sites.h"

namespace {

using partner_sites::Kind;
using partner_sites::Site;
using partner_sites::Thunk;

constexpr uintptr_t kJuicedMargin = 16;
constexpr size_t kBranchBytes = 5;
constexpr size_t kDwordBytes = 4;
constexpr size_t kTotalRows = 18;
constexpr size_t kWrittenSites = 11;
constexpr size_t kCheckRows = 7;
constexpr size_t kSetCount = 4;
constexpr uintptr_t kThunk = 0x10004000;
constexpr uint8_t kCallOpcode = 0xE8;
constexpr uint8_t kJumpOpcode = 0xE9;
constexpr uint8_t kNop = 0x90;
constexpr uint8_t kShortJumpOpcode = 0xEB;
constexpr uint8_t kObjectTail = 6;                 // TB7: the short jump over the dead bytes
constexpr uintptr_t kObjectLoopNext = 0x58DC80;    // TB7: where the loop goes on
constexpr size_t kObjectCallBytes = 13;

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_failures;
}

std::vector<Site> ourSites() {
    std::vector<Site> all;
    for (const trail_blazing_sites::Set& set : trail_blazing_sites::kSets) all.insert(all.end(), set.sites.begin(), set.sites.end());
    return all;
}

size_t sizeOf(std::string_view hex) { return partner_sites::parseHex(hex).size(); }

size_t sizeOf(const Site& site) { return sizeOf(site.expected); }

const Site& siteNamed(const std::vector<Site>& sites, std::string_view id) {
    return *std::find_if(sites.begin(), sites.end(), [&](const Site& site) { return id == site.id; });
}

// Where the `call rel32` at the start of a site's bytes (or at `offset` into them) goes.
uintptr_t callTarget(const Site& site, size_t offset = 0) {
    const auto bytes = partner_sites::parseHex(site.expected);
    const uint32_t displacement = bytes[offset + 1] | bytes[offset + 2] << 8 | bytes[offset + 3] << 16 | static_cast<uint32_t>(bytes[offset + 4]) << 24;
    return site.address + offset + kBranchBytes + displacement;
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
                             std::span<const Site>(outfit_sync_sites::kWeaponSlots), std::span<const Site>(outfit_sync_sites::kTraces),
                             std::span<const Site>(vehicle_seating_sites::kTrafficking), std::span<const Site>(vehicle_seating_sites::kCheckpoint),
                             std::span<const Site>(vehicle_seating_sites::kMissionExit), std::span<const Site>(revive_sites::kRevive),
                             std::span<const Site>(revive_sites::kNotoriety), std::span<const Site>(mission_flow_sites::kPrompts),
                             std::span<const Site>(mission_flow_sites::kReplay), std::span<const Site>(mission_flow_sites::kRewards),
                             std::span<const Site>(mission_flow_sites::kTrace)}) {
        add(table);
    }
    for (const session_kick_sites::Set& set : session_kick_sites::kSets) add(set.sites);
    for (const store_sites::Set& set : store_sites::kSets) add(set.sites);
    for (const hud_sites::Set& set : hud_sites::kSets) add(set.sites);
    for (const net_handlers_sites::Set& set : net_handlers_sites::kSets) add(set.sites);
    for (const party_ai_sites::Phase& phase : party_ai_sites::kPhases) add(phase.sites);
    for (const anim_stream_sites::Phase& phase : anim_stream_sites::kPhases) {
        add(phase.sites);
        for (const anim_stream_sites::NeedTableSite& site : phase.immediates) others.push_back({site.address, sizeOf(site.expected)});
    }
    for (const char_types_sites::Row& row : char_types_sites::kRows) others.push_back({row.address, sizeOf(row.expected)});
    return others;
}

// The ids of the tables of milestones 4 and 7 and of the diversions.
std::set<std::string_view> otherIds() {
    std::set<std::string_view> ids;
    const auto add = [&ids](std::span<const Site> table) {
        for (const Site& site : table) ids.insert(site.id);
    };
    add(partner_sites::kSites);
    add(activity_sites::kPhase1);
    add(activity_sites::kPhase2);
    add(activity_sites::kPhase3);
    for (const diversion_sites::Set& set : diversion_sites::kSets) add(set.sites);
    return ids;
}

size_t setSize(std::string_view name) {
    for (const trail_blazing_sites::Set& set : trail_blazing_sites::kSets) {
        if (name == set.name) return set.sites.size();
    }
    return 0;
}

void theTables() {
    const std::vector<Site> sites = ourSites();
    const std::set<std::string_view> others = otherIds();
    std::set<std::string_view> ids;
    std::set<uintptr_t> addresses;
    bool unique = true;
    for (const Site& site : sites) unique = unique && ids.insert(site.id).second && addresses.insert(site.address).second && others.count(site.id) == 0;
    check(unique, "ids and addresses are unique, and no id is used by another milestone");
    const auto written = std::count_if(sites.begin(), sites.end(), [](const Site& site) { return site.kind != Kind::Bytes; });
    check(sites.size() == kTotalRows && static_cast<size_t>(written) == kWrittenSites && sites.size() - kWrittenSites == kCheckRows,
          "18 rows: 11 written sites and 7 check rows");
    check(std::size(trail_blazing_sites::kSets) == kSetCount && setSize("burn") == 8 && setSize("fire") == 4 && setSize("start") == 4 &&
              setSize("checkpoint") == 2,
          "four sets that partition the rows: burn 8, fire 4, start 4, checkpoint 2");
    check(std::all_of(sites.begin(), sites.end(), [](const Site& site) { return (site.kind == Kind::Bytes) == (site.thunk == Thunk::None); }),
          "a check row has no thunk and every written site has one");
    check(std::all_of(sites.begin(), sites.end(), [](const Site& site) { return site.kind == Kind::Bytes ? site.tail.empty() : true; }),
          "a check row has no tail");
}

void theCalls() {
    const std::vector<Site> sites = ourSites();
    bool calls = true;
    bool keepsOpcode = true;
    for (const char* id : {"TB1", "TB4", "TB5"}) {
        const Site& site = siteNamed(sites, id);
        calls = calls && site.kind == Kind::RetargetCall && sizeOf(site) == kBranchBytes && partner_sites::parseHex(site.expected)[0] == kCallOpcode;
        const auto written = partner_replacement::replacementFor(site, kThunk);
        keepsOpcode = keepsOpcode && written.size() == kBranchBytes && written[0] == kCallOpcode;
    }
    check(calls, "TB1, TB4 and TB5 are 5 byte call retargets");
    check(keepsOpcode, "each retargeted call keeps its opcode and length");
    check(callTarget(siteNamed(sites, "TB1")) == sr2::kRemotePlayerGetter, "TB1 replaces a call of the partner getter 0x9D7BD0");
    check(callTarget(siteNamed(sites, "TB4")) == sr2::kPutOutHuman && callTarget(siteNamed(sites, "TB5")) == sr2::kPutOutHuman,
          "TB4 and TB5 replace a call of the put out 0x98CEB0");
    check(siteNamed(sites, "TB4").thunk == siteNamed(sites, "TB5").thunk, "the cleanup and the end share a thunk");
}

void theObjectBlock() {
    const std::vector<Site> sites = ourSites();
    const Site& block = siteNamed(sites, "TB7");
    check(block.kind == Kind::CallBlock && sizeOf(block) == kObjectCallBytes && sizeOf(block.tail) == 2, "TB7 is a 13 byte call block with a 2 byte tail");
    check(callTarget(block, kObjectCallBytes - kBranchBytes) == sr2::kTrailLightObjectNear, "TB7's bytes end in a call of 0x58D9B0");
    const auto written = partner_replacement::replacementFor(block, kThunk);
    const size_t tailAt = kBranchBytes;
    check(written.size() == kBranchBytes + 2 && written[0] == kCallOpcode && written[tailAt] == kShortJumpOpcode && written[tailAt + 1] == kObjectTail,
          "TB7 is written as a call and EB 06");
    check(block.address + tailAt + 2 + written[tailAt + 1] == kObjectLoopNext, "the tail of TB7 lands on 0x58DC80, past the dead bytes");
}

void theJumpOuts() {
    const std::vector<Site> sites = ourSites();
    struct Block {
        const char* jump;
        const char* checkRow;
        uintptr_t resume;
    };
    bool shapes = true;
    bool resumes = true;
    bool written = true;
    for (const Block block : {Block{"TB2", "TB2r", sr2::kResumeTrailLevelStart}, Block{"TB3", "TB3r", sr2::kResumeTrailLevelStep},
                              Block{"TB6", "TB6r", sr2::kResumeTrailFireNodes}, Block{"TB8", "TB8r", sr2::kResumeTrailWaterFail},
                              Block{"TB9", "TB9r", sr2::kResumeTrailStartPlace}, Block{"TB10", "TB10r", sr2::kResumeTrailStartExit},
                              Block{"TB11", "TB11r", sr2::kResumeTrailCheckpoint}}) {
        const Site& jump = siteNamed(sites, block.jump);
        const Site& row = siteNamed(sites, block.checkRow);
        shapes = shapes && jump.kind == Kind::JumpOut && sizeOf(jump) >= kBranchBytes && row.kind == Kind::Bytes;
        resumes = resumes && row.address == block.resume && row.address > jump.address;
        const auto bytes = partner_replacement::replacementFor(jump, kThunk);
        written = written && bytes.size() == sizeOf(jump) && bytes[0] == kJumpOpcode &&
                  std::all_of(bytes.begin() + kBranchBytes, bytes.end(), [](uint8_t byte) { return byte == kNop; });
        written = written && partner_replacement::replacementFor(row, kThunk).empty();
    }
    check(shapes, "TB2, TB3, TB6, TB8, TB9, TB10 and TB11 are jump outs of at least 5 bytes, each with a check row");
    check(resumes, "each check row is the address its stub resumes at");
    check(written, "a jump out is written as E9 and nops over its bytes, a check row writes nothing");
}

void theClearance() {
    const std::vector<Site> sites = ourSites();
    bool apart = true;
    for (const Site& a : sites) {
        for (const Site& b : sites) {
            if (&a != &b && other_sites::overlaps(a.address, sizeOf(a), b.address, sizeOf(b))) apart = false;
        }
    }
    check(apart, "no two rows overlap");

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
    check(clear, "no row overlaps a site of an earlier milestone");
    check(clearOfJuiced, "no row is within 16 bytes of a Juiced target");
}

}  // namespace

int main() {
    theTables();
    theCalls();
    theObjectBlock();
    theJumpOuts();
    theClearance();
    return g_failures == 0 ? 0 : 1;
}
