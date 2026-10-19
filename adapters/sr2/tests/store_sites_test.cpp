// The milestone 16 wave 2 store site tables (no game): ids and addresses, shapes, the callee of each retargeted call, the jump out blocks
// with their check rows and resume addresses, the sets, and clearance from every other site and from everything Juiced touches.
#include <algorithm>
#include <cstdio>
#include <set>
#include <span>
#include <string_view>
#include <vector>

#include "../src/anim_stream_sites.h"
#include "../src/char_types_sites.h"
#include "../src/multiplayer_sites.h"
#include "../src/outfit_sync_sites.h"
#include "../src/partner_replacement.h"
#include "../src/session_kick_sites.h"
#include "../src/sr2_addresses.h"
#include "../src/store_sites.h"
#include "../src/vehicle_pool_references.h"
#include "../src/vehicle_pool_sites.h"
#include "juiced_targets.h"
#include "other_sites.h"

namespace {

using partner_sites::Kind;
using partner_sites::Site;
using partner_sites::Thunk;

constexpr uintptr_t kJuicedMargin = 16;
constexpr size_t kBranchBytes = 5;
constexpr size_t kDwordBytes = 4;
constexpr size_t kBlockBytes = 39;
constexpr uintptr_t kThunk = 0x10004000;
constexpr uint8_t kCallOpcode = 0xE8;
constexpr uint8_t kJumpOpcode = 0xE9;
constexpr uint8_t kNop = 0x90;
constexpr size_t kWrittenSites = 8;
constexpr size_t kCheckRows = 3;

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_failures;
}

std::vector<Site> ourSites() {
    std::vector<Site> all;
    for (const store_sites::Set& set : store_sites::kSets) all.insert(all.end(), set.sites.begin(), set.sites.end());
    return all;
}

size_t sizeOf(std::string_view hex) { return partner_sites::parseHex(hex).size(); }

size_t sizeOf(const Site& site) { return sizeOf(site.expected); }

const Site* siteNamed(const std::vector<Site>& sites, std::string_view id) {
    for (const Site& site : sites) {
        if (id == site.id) return &site;
    }
    return nullptr;
}

size_t setSize(std::string_view name) {
    for (const store_sites::Set& set : store_sites::kSets) {
        if (name == set.name) return set.sites.size();
    }
    return 0;
}

// Where the `call rel32` of a retargeted call site goes.
uintptr_t callTarget(const Site& site) {
    const auto bytes = partner_sites::parseHex(site.expected);
    const uint32_t displacement = bytes[1] | bytes[2] << 8 | bytes[3] << 16 | static_cast<uint32_t>(bytes[4]) << 24;
    return site.address + kBranchBytes + displacement;
}

// Every site table of the other milestones (milestone 16 wave 1 included), by range.
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
    for (const auto& bound : vehicle_pool_sites::kBounds) others.push_back({bound.address, partner_sites::parseHex(bound.expected).size()});
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
    for (const auto table : {std::span<const Site>(outfit_sync_sites::kLookSync), std::span<const Site>(outfit_sync_sites::kItemWear),
                             std::span<const Site>(outfit_sync_sites::kWeaponSlots), std::span<const Site>(outfit_sync_sites::kTraces)}) {
        add(table);
    }
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
    const auto written = std::count_if(sites.begin(), sites.end(), [](const Site& site) { return site.kind != Kind::Bytes; });
    check(static_cast<size_t>(written) == kWrittenSites && sites.size() == kWrittenSites + kCheckRows, "8 written sites and 3 check rows");
    check(setSize("hide") == 6 && setSize("garage") == 1 && setSize("activity level") == 4, "three sets: hide 6 rows, garage 1, activity level 4");
    check(std::all_of(sites.begin(), sites.end(), [](const Site& site) { return site.gate == partner_sites::Gate::Now && site.tail.empty(); }),
          "every site is written when its set is (no probe gate, no tail)");
    check(std::all_of(sites.begin(), sites.end(), [](const Site& site) { return (site.kind == Kind::Bytes) == (site.thunk == Thunk::None); }),
          "a check row has no thunk and every written site has one");
}

void theCalls() {
    const std::vector<Site> sites = ourSites();
    bool calls = true;
    bool keepsOpcode = true;
    for (const char* id : {"GB1", "LM1", "LM2", "LM3", "LM4"}) {
        const Site* site = siteNamed(sites, id);
        const auto bytes = partner_sites::parseHex(site->expected);
        calls = calls && site->kind == Kind::RetargetCall && bytes.size() == kBranchBytes && bytes[0] == kCallOpcode;
        const auto written = partner_replacement::replacementFor(*site, kThunk);
        keepsOpcode = keepsOpcode && written.size() == kBranchBytes && written[0] == kCallOpcode;
    }
    check(calls, "GB1 and LM1 to LM4 are 5 byte call retargets");
    check(keepsOpcode, "each retargeted call keeps its opcode and length");
    check(callTarget(*siteNamed(sites, "GB1")) == sr2::kPointInBox, "GB1 calls the point in box test 0xBE9890");
    check(callTarget(*siteNamed(sites, "LM1")) == sr2::kRemotePlayerGetter, "LM1 calls get_remote_player 0x9D7BD0");
    check(callTarget(*siteNamed(sites, "LM2")) == sr2::kSendBroadcast && callTarget(*siteNamed(sites, "LM3")) == sr2::kSendBroadcast &&
              callTarget(*siteNamed(sites, "LM4")) == sr2::kSendBroadcast,
          "LM2 to LM4 call the broadcast 0x814740");
    check(siteNamed(sites, "LM3")->thunk == siteNamed(sites, "LM4")->thunk && siteNamed(sites, "LM2")->thunk != siteNamed(sites, "LM3")->thunk,
          "the choice and the cancel share a thunk, the request has its own");
    check(siteNamed(sites, "LM3")->address + kBranchBytes == sr2::kLevelChoiceSendReturn &&
              siteNamed(sites, "LM4")->address + kBranchBytes == sr2::kLevelCancelSendReturn,
          "the return addresses the answer trace tells apart are the ends of LM3 and LM4");
}

void theBlocks() {
    const std::vector<Site> sites = ourSites();
    struct Block {
        const char* jump;
        const char* checkRow;
        uintptr_t resume;
    };
    bool shapes = true;
    bool resumes = true;
    bool written = true;
    for (const Block block : {Block{"SH1", "SH1r", sr2::kStoreEnterHideResume}, Block{"SH2", "SH2r", sr2::kStoreExitShowResume},
                              Block{"SH3", "SH3r", sr2::kGarageHideResume}}) {
        const Site* jump = siteNamed(sites, block.jump);
        const Site* row = siteNamed(sites, block.checkRow);
        shapes = shapes && jump && row && jump->kind == Kind::JumpOut && sizeOf(*jump) == kBlockBytes && row->kind == Kind::Bytes;
        resumes = resumes && jump && row && row->address == jump->address + kBlockBytes && row->address == block.resume;
        const auto bytes = partner_replacement::replacementFor(*jump, kThunk);
        written = written && bytes.size() == kBlockBytes && bytes[0] == kJumpOpcode &&
                  std::all_of(bytes.begin() + kBranchBytes, bytes.end(), [](uint8_t byte) { return byte == kNop; });
        written = written && partner_replacement::replacementFor(*row, kThunk).empty();
    }
    check(shapes, "SH1 to SH3 are 39 byte jump outs, each with a check row");
    check(resumes, "each check row is the byte after its block and the address the stub resumes at");
    check(written, "a jump out is written as E9 and nops to 39 bytes, a check row writes nothing");
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
    check(clear, "no site overlaps a site of an earlier milestone, wave 1 included");
    check(clearOfJuiced, "no site is within 16 bytes of a Juiced target");
}

}  // namespace

int main() {
    theTables();
    theCalls();
    theBlocks();
    theClearance();
    return g_failures == 0 ? 0 : 1;
}
