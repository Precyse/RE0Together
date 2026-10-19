// The milestone 17 wave 3 site tables (no game): the 13 rows and their seven sets, ids and addresses, the callee of each retargeted
// call, the pose block with its branch targets, the follower bars jump out, the two pointer swaps, and clearance from every other
// site and from everything Juiced touches.
#include <algorithm>
#include <cstdio>
#include <set>
#include <span>
#include <string_view>
#include <vector>

#include "../src/activity_sites.h"
#include "../src/anim_stream_sites.h"
#include "../src/char_types_sites.h"
#include "../src/diversion_sites.h"
#include "../src/hud_sites.h"
#include "../src/mission_flow_sites.h"
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
#include "../src/world_walks_sites.h"
#include "juiced_targets.h"
#include "other_sites.h"

namespace {

using partner_sites::Kind;
using partner_sites::Site;
using partner_sites::Thunk;

constexpr uintptr_t kJuicedMargin = 16;
constexpr size_t kBranchBytes = 5;
constexpr size_t kDwordBytes = 4;
constexpr size_t kPoseBlockBytes = 14;
constexpr size_t kPoseTailBytes = 9;
constexpr size_t kFollowerBarsBytes = 6;
constexpr size_t kWrittenSites = 10;
constexpr size_t kCheckRows = 3;
constexpr size_t kSetCount = 7;
constexpr uintptr_t kThunk = 0x10004000;
constexpr uint8_t kCallOpcode = 0xE8;
constexpr uint8_t kShortJumpIfEqual = 0x74;
constexpr uint8_t kNop = 0x90;
constexpr uintptr_t kPoseSelectNormalState = 0x96F1C6;  // PD2b
constexpr uintptr_t kPoseSelectReturn = 0x96F1C1;       // PD2a
constexpr uintptr_t kAmbulanceHandler = 0x5F7E70;
constexpr uintptr_t kCompletionHandler = 0x8CA660;

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_failures;
}

std::vector<Site> ourSites() {
    std::vector<Site> all;
    for (const world_walks_sites::Set& set : world_walks_sites::kSets) all.insert(all.end(), set.sites.begin(), set.sites.end());
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

size_t setSize(size_t index) { return world_walks_sites::kSets[index].sites.size(); }

// The dword a pointer swap row holds.
uintptr_t pointerIn(const Site& site) {
    const auto bytes = partner_sites::parseHex(site.expected);
    return bytes[0] | bytes[1] << 8 | bytes[2] << 16 | static_cast<uintptr_t>(bytes[3]) << 24;
}

// Where the `call rel32` at the start of a retargeted call site goes.
uintptr_t callTarget(const Site& site) {
    const auto bytes = partner_sites::parseHex(site.expected);
    const uint32_t displacement = bytes[1] | bytes[2] << 8 | bytes[3] << 16 | static_cast<uint32_t>(bytes[4]) << 24;
    return site.address + kBranchBytes + displacement;
}

// Every site table of the other milestones.
std::vector<std::span<const Site>> earlierTables() {
    std::vector<std::span<const Site>> tables = {partner_sites::kSites,
                                                 activity_sites::kPhase1,
                                                 activity_sites::kPhase2,
                                                 activity_sites::kPhase3,
                                                 outfit_sync_sites::kLookSync,
                                                 outfit_sync_sites::kItemWear,
                                                 outfit_sync_sites::kWeaponSlots,
                                                 outfit_sync_sites::kTraces,
                                                 revive_sites::kRevive,
                                                 revive_sites::kNotoriety,
                                                 vehicle_seating_sites::kTrafficking,
                                                 vehicle_seating_sites::kCheckpoint,
                                                 vehicle_seating_sites::kMissionExit,
                                                 vehicle_pool_sites::kHeadroomBlocks};
    for (const auto table : multiplayer_sites::kScriptTraces) tables.push_back(table);
    for (const multiplayer_sites::Phase& phase : multiplayer_sites::kPhases) tables.push_back(phase.sites);
    for (const diversion_sites::Set& set : diversion_sites::kSets) tables.push_back(set.sites);
    for (const hud_sites::Set& set : hud_sites::kSets) tables.push_back(set.sites);
    for (const store_sites::Set& set : store_sites::kSets) tables.push_back(set.sites);
    for (const net_handlers_sites::Set& set : net_handlers_sites::kSets) tables.push_back(set.sites);
    for (const session_kick_sites::Set& set : session_kick_sites::kSets) tables.push_back(set.sites);
    for (const party_ai_sites::Phase& phase : party_ai_sites::kPhases) tables.push_back(phase.sites);
    for (const anim_stream_sites::Phase& phase : anim_stream_sites::kPhases) tables.push_back(phase.sites);
    return tables;
}

// Every range of the exe another milestone writes, by address and size.
std::vector<other_sites::Range> earlierRanges() {
    std::vector<other_sites::Range> others = other_sites::milestones3to8();
    for (const auto table : earlierTables()) {
        for (const Site& site : table) others.push_back({site.address, sizeOf(site)});
    }
    for (const player_sites::SkinPoolSite& site : player_sites::kSkinPool) others.push_back({site.address, sizeOf(site.expected)});
    for (const auto& row : vehicle_pool_references::kReferences) others.push_back({row.site, kDwordBytes});
    for (const auto& bound : vehicle_pool_sites::kBounds) others.push_back({bound.address, sizeOf(bound.expected)});
    for (const anim_stream_sites::Phase& phase : anim_stream_sites::kPhases) {
        for (const anim_stream_sites::NeedTableSite& site : phase.immediates) others.push_back({site.address, sizeOf(site.expected)});
    }
    for (const char_types_sites::Row& row : char_types_sites::kRows) others.push_back({row.address, sizeOf(row.expected)});
    return others;
}

std::set<std::string_view> otherIds() {
    std::set<std::string_view> ids;
    for (const auto table : earlierTables()) {
        for (const Site& site : table) ids.insert(site.id);
    }
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
    check(static_cast<size_t>(written) == kWrittenSites && sites.size() == kWrittenSites + kCheckRows, "10 written sites and 3 check rows");
    check(std::size(world_walks_sites::kSets) == kSetCount, "seven sets");
    check(setSize(0) == 5 && setSize(1) == 1 && setSize(2) == 2 && setSize(3) == 1 && setSize(4) == 1 && setSize(5) == 1 && setSize(6) == 2,
          "the sets hold 5, 1, 2, 1, 1, 1 and 2 rows");
    bool names = true;
    for (const world_walks_sites::Set& set : world_walks_sites::kSets) {
        names = names && (set.needsActivities == (std::string_view(set.name) == "activity warp gate"));
    }
    check(names, "only the activity warp gate waits for the activity logic");
    check(std::all_of(sites.begin(), sites.end(), [](const Site& site) { return (site.kind == Kind::Bytes) == (site.thunk == Thunk::None); }),
          "a check row has no thunk and every written row has one");
    check(std::all_of(sites.begin(), sites.end(), [](const Site& site) { return (site.kind == Kind::CallBlock) == !site.tail.empty(); }),
          "only the call block has a tail");
}

void theCalls() {
    const std::vector<Site> sites = ourSites();
    bool calls = true;
    bool keepsOpcode = true;
    for (const char* id : {"PD1", "SW1", "AW1", "GZ1", "VF1", "CN2"}) {
        const Site* site = siteNamed(sites, id);
        const auto bytes = partner_sites::parseHex(site->expected);
        calls = calls && site->kind == Kind::RetargetCall && bytes.size() == kBranchBytes && bytes[0] == kCallOpcode;
        const auto written = partner_replacement::replacementFor(*site, kThunk);
        keepsOpcode = keepsOpcode && written.size() == kBranchBytes && written[0] == kCallOpcode;
    }
    check(calls, "PD1, SW1, AW1, GZ1, VF1 and CN2 are 5 byte call retargets");
    check(keepsOpcode, "each retargeted call keeps its opcode and length");
    bool getters = true;
    for (const char* id : {"PD1", "AW1", "GZ1", "VF1", "CN2"}) getters = getters && callTarget(*siteNamed(sites, id)) == sr2::kRemotePlayerGetter;
    check(getters, "PD1, AW1, GZ1, VF1 and CN2 call get_remote_player 0x9D7BD0");
    check(callTarget(*siteNamed(sites, "SW1")) == sr2::kSpawnSeenByHuman, "SW1 calls the seen-by-human test 0x936080");
    check(siteNamed(sites, "PD1")->thunk == Thunk::MatchRemoteEdi && siteNamed(sites, "VF1")->thunk == Thunk::MatchRemoteEsi,
          "PD1 and VF1 reuse the identity thunks");
}

void thePoseBlock() {
    const std::vector<Site> sites = ourSites();
    const Site& block = *siteNamed(sites, "PD2");
    const auto bytes = partner_sites::parseHex(block.expected);
    check(block.kind == Kind::CallBlock && bytes.size() == kPoseBlockBytes && bytes[0] == kCallOpcode, "PD2 is a 14 byte block that starts with a call");
    const auto tail = partner_sites::parseHex(block.tail);
    check(tail.size() == kPoseTailBytes && kBranchBytes + tail.size() == kPoseBlockBytes, "PD2's call and tail fill the block");
    const auto written = partner_replacement::replacementFor(block, kThunk);
    check(written.size() == kPoseBlockBytes && written[0] == kCallOpcode, "PD2 is written as a call and its tail, 14 bytes");
    const uintptr_t branchAt = block.address + kBranchBytes + 3;  // after `cmp eax, -1`
    check(tail[3] == kShortJumpIfEqual && branchAt + 2 + tail[4] == kPoseSelectNormalState && kPoseSelectNormalState == siteNamed(sites, "PD2b")->address,
          "the je of the tail lands on PD2b, the normal state");
    check(block.address + kPoseBlockBytes == kPoseSelectReturn && kPoseSelectReturn == siteNamed(sites, "PD2a")->address,
          "the byte after the block is PD2a, which returns eax");
    check(std::all_of(tail.begin() + 5, tail.end(), [](uint8_t byte) { return byte == kNop; }), "the tail ends in nops into PD2a");
    check(siteNamed(sites, "PD2a")->kind == Kind::Bytes && siteNamed(sites, "PD2b")->kind == Kind::Bytes && siteNamed(sites, "PD2a")->tail.empty() &&
              siteNamed(sites, "PD2b")->tail.empty(),
          "PD2a and PD2b are check rows");
}

void theJumpOutAndSwaps() {
    const std::vector<Site> sites = ourSites();
    const Site& bars = *siteNamed(sites, "FB1");
    check(bars.kind == Kind::JumpOut && sizeOf(bars) == kFollowerBarsBytes, "FB1 is a 6 byte jump out");
    check(siteNamed(sites, "FB1r")->address == bars.address + kFollowerBarsBytes && siteNamed(sites, "FB1r")->address == sr2::kFollowerBarsResume &&
              siteNamed(sites, "FB1r")->kind == Kind::Bytes,
          "FB1r is the byte after FB1 and the address the stub resumes at");
    const auto jump = partner_replacement::replacementFor(bars, kThunk);
    check(jump.size() == kFollowerBarsBytes && jump[0] == kJmpRel32 && jump[kBranchBytes] == kNop, "FB1 is written as a jmp and one nop");
    const Site& poses = *siteNamed(sites, "PD3");
    check(poses.kind == Kind::PointerSwap && poses.address == sr2::kActHandlerTable + sr2::kActAmbulancePoseSub * sizeof(uint32_t) &&
              pointerIn(poses) == kAmbulanceHandler,
          "PD3 holds the act 0x4B handler 0x5F7E70 in the act table");
    const Site& completion = *siteNamed(sites, "CN1");
    check(completion.kind == Kind::PointerSwap && completion.address == sr2::kMiscHandlerTable + sr2::kMiscCompletionSub * sizeof(uint32_t) &&
              pointerIn(completion) == kCompletionHandler,
          "CN1 holds the misc 7 handler 0x8CA660 in the misc table");
    const auto swapped = partner_replacement::replacementFor(poses, kThunk);
    check(swapped.size() == kDwordBytes && swapped[0] == (kThunk & 0xFF) && swapped[3] == (kThunk >> 24), "a pointer swap writes the thunk address");
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
    thePoseBlock();
    theJumpOutAndSwaps();
    theClearance();
    return g_failures == 0 ? 0 : 1;
}
