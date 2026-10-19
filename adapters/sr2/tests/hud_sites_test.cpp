// The milestone 16 wave 2 site tables (no game): the 17 rows and their five sets, ids and addresses, shapes, the callee of each
// call, the resume addresses of the looped blocks, and clearance from every other site and from everything Juiced touches.
#include <algorithm>
#include <cstdio>
#include <set>
#include <span>
#include <string_view>
#include <vector>

#include "../src/hud_sites.h"
#include "../src/multiplayer_sites.h"
#include "../src/outfit_sync_sites.h"
#include "../src/partner_replacement.h"
#include "../src/player_sites.h"
#include "../src/session_kick_sites.h"
#include "../src/sr2_addresses.h"
#include "juiced_targets.h"
#include "other_sites.h"

namespace {

using partner_sites::Kind;
using partner_sites::Site;
using partner_sites::Thunk;

constexpr uintptr_t kClearance = 16;
constexpr size_t kBranchBytes = 5;
constexpr size_t kHookBytes = 5;
constexpr size_t kDwordBytes = 4;
constexpr uintptr_t kThunk = 0x10004000;
constexpr uint8_t kCallOpcode = 0xE8;
constexpr size_t kRows = 17;
constexpr size_t kWrittenRows = 13;
constexpr size_t kCheckRows = 4;
constexpr size_t kSets = 5;

// The dword operands Juiced's range scans in DFEngine.dll rewrite (the HUD scale floats 0x022DBFAC and 0x022DBFA8) in the minimap
// block 0x7A37E0..0x7A3D2F, and the sites they must stay clear of.
constexpr uintptr_t kJuicedMinimapOperands[] = {0x7A383C, 0x7A3876, 0x7A38B9, 0x7A38BF, 0x7A3B5E, 0x7A3B64, 0x7A3C13, 0x7A3C19};

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_failures;
}

std::vector<Site> everySite() {
    std::vector<Site> all;
    for (const hud_sites::Set& set : hud_sites::kSets) all.insert(all.end(), set.sites.begin(), set.sites.end());
    return all;
}

size_t sizeOf(const Site& site) { return partner_sites::parseHex(site.expected).size(); }

const Site* siteNamed(const std::vector<Site>& sites, std::string_view id) {
    for (const Site& site : sites) {
        if (id == site.id) return &site;
    }
    return nullptr;
}

// The rel32 that ends the bytes of a site, as a displacement.
uint32_t trailingDisplacement(const Site& site) {
    const auto bytes = partner_sites::parseHex(site.expected);
    const size_t size = bytes.size();
    return bytes[size - 4] | bytes[size - 3] << 8 | bytes[size - 2] << 16 | static_cast<uint32_t>(bytes[size - 1]) << 24;
}

// Where the `call rel32` at the start of a site goes.
uintptr_t callTarget(const Site& site) {
    const auto bytes = partner_sites::parseHex(site.expected);
    const uint32_t displacement = bytes[1] | bytes[2] << 8 | bytes[3] << 16 | static_cast<uint32_t>(bytes[4]) << 24;
    return site.address + kBranchBytes + displacement;
}

size_t setSize(std::string_view name) {
    for (const hud_sites::Set& set : hud_sites::kSets) {
        if (name == set.name) return set.sites.size();
    }
    return 0;
}

void theTables() {
    const std::vector<Site> all = everySite();
    std::set<std::string_view> ids;
    std::set<uintptr_t> addresses;
    bool unique = true;
    for (const Site& site : all) unique = ids.insert(site.id).second && addresses.insert(site.address).second && unique;
    check(all.size() == kRows, "17 rows");
    check(unique, "ids and addresses are unique");
    check(std::size(hud_sites::kSets) == kSets, "five sets");
    const size_t checkOnly = static_cast<size_t>(std::count_if(all.begin(), all.end(), [](const Site& s) { return s.kind == Kind::Bytes; }));
    check(checkOnly == kCheckRows, "4 rows are check only");
    check(all.size() - checkOnly == kWrittenRows, "13 rows are written");
    check(setSize("minimap") == 6 && setSize("pause map") == 4 && setSize("back prompt") == 1 && setSize("map distance") == 2 &&
              setSize("waypoint scope") == 4,
          "the sets hold 6, 4, 1, 2 and 4 rows");
    check(std::all_of(all.begin(), all.end(), [](const Site& site) { return site.gate == partner_sites::Gate::Now && site.tail.empty(); }),
          "every row is written when its set is (no probe gate, no tail)");
    check(std::all_of(all.begin(), all.end(), [](const Site& site) { return (site.kind == Kind::Bytes) == (site.thunk == Thunk::None); }),
          "every row but a check row has a thunk");
}

void theShapes() {
    const std::vector<Site> all = everySite();
    bool shapes = true;
    for (const Site& site : all) {
        const size_t size = sizeOf(site);
        if (site.kind == Kind::JumpOut) shapes = shapes && size >= kBranchBytes;
        if (site.kind == Kind::RetargetCall) shapes = shapes && size == kBranchBytes && partner_sites::parseHex(site.expected)[0] == kCallOpcode;
        if (site.kind == Kind::HookEntry) shapes = shapes && size >= kHookBytes;
    }
    check(shapes, "jump outs hold 5 bytes or more, retargeted calls are 5 byte calls, hooks hold 5 bytes or more");
    bool replaced = true;
    for (const Site& site : all) {
        if (site.kind != Kind::RetargetCall) continue;
        const auto bytes = partner_replacement::replacementFor(site, kThunk);
        replaced = replaced && bytes.size() == kBranchBytes && bytes[0] == kCallOpcode;
    }
    check(replaced, "each retargeted call keeps its opcode and length");
    const Site& hm1 = *siteNamed(all, "HM1");
    const auto branch = relativeBranch(kCallOpcode, hm1.address, kThunk);
    check(partner_replacement::replacementFor(hm1, kThunk) == std::vector<uint8_t>(branch.begin(), branch.end()),
          "HM1 gets a call to the thunk at its address");
    const Site& hm3 = *siteNamed(all, "HM3");
    const auto join = partner_replacement::replacementFor(hm3, kThunk);
    check(join.size() == sizeOf(hm3) && join[0] == kJmpRel32 &&
              std::all_of(join.begin() + kBranchBytes, join.end(), [](uint8_t b) { return b == partner_replacement::kNop; }),
          "HM3 gets a jmp to the thunk and nops over the rest of the join");
}

void theCallees() {
    const std::vector<Site> all = everySite();
    const auto callee = [&](std::string_view id) { return callTarget(*siteNamed(all, id)); };
    check(callee("HM1") == sr2::kFollowerBlipsDraw, "HM1 calls the follower blips 0x79F890");
    bool getters = true;
    for (const std::string_view id : {"HM2", "HM4", "HP1", "HP1r", "HQ1", "HQ2", "HQ3"}) getters = getters && callee(id) == sr2::kRemotePlayerGetter;
    check(getters, "HM2, HM4, HP1, HP1r, HQ1, HQ2 and HQ3 call the getter 0x9D7BD0");
    check(callee("GP2") == sr2::kSendBroadcast && callee("GP3") == sr2::kSendBroadcast && callee("GP4") == sr2::kSendBroadcast,
          "GP2, GP3 and GP4 call the broadcast 0x814740");
    check(siteNamed(all, "GP1")->address == sr2::kMissionDebugNative, "GP1 hooks mission_debug 0xA398C0");
}

void theResumeAddresses() {
    const std::vector<Site> all = everySite();
    const auto after = [&](std::string_view id) { return siteNamed(all, id)->address + sizeOf(*siteNamed(all, id)); };
    check(siteNamed(all, "HM2r")->address == sr2::kMinimapArrowBody && after("HM2") == sr2::kMinimapArrowBody,
          "the minimap body starts where HM2's replaced bytes end, 0x7A3AA4");
    check(siteNamed(all, "HM3")->address == sr2::kMinimapArrowExit && after("HM3") == sr2::kMinimapArrowAfterExit &&
              siteNamed(all, "HM3r")->address == sr2::kMinimapArrowAfterExit,
          "the minimap exit is HM3, the instruction after it 0x7A3BE2 is checked by HM3r");
    check(siteNamed(all, "HP1r")->address == sr2::kPauseMapArrowBody && after("HP1") == sr2::kPauseMapArrowBody,
          "the pause map body starts where HP1's replaced bytes end, 0x77029A");
    check(siteNamed(all, "HP2")->address == sr2::kPauseMapArrowExit && after("HP2") == sr2::kPauseMapArrowAfterExit &&
              siteNamed(all, "HP2r")->address == sr2::kPauseMapArrowAfterExit,
          "the pause map exit is HP2, the instruction after it 0x770394 is checked by HP2r");
    check(after("HM2") + trailingDisplacement(*siteNamed(all, "HM2")) == sr2::kMinimapArrowExit, "HM2's je goes to the minimap exit, so no remote resumes there");
    check(after("HP1") + trailingDisplacement(*siteNamed(all, "HP1")) == sr2::kPauseMapArrowExit, "HP1's je goes to the pause map exit, so no remote resumes there");
}

std::vector<other_sites::Range> earlierRanges() {
    std::vector<other_sites::Range> others = other_sites::milestones3to8();
    const auto activities = other_sites::activities();
    const auto crowd = other_sites::players3and4();
    others.insert(others.end(), activities.begin(), activities.end());
    others.insert(others.end(), crowd.begin(), crowd.end());
    for (const auto table : multiplayer_sites::kScriptTraces) {
        for (const Site& site : table) others.push_back({site.address, sizeOf(site)});
    }
    for (const auto table : {std::span<const Site>(player_sites::kNetClock), std::span<const Site>(player_sites::kBundleSender),
                             std::span<const Site>(player_sites::kCutsceneVisuals), std::span<const Site>(player_sites::kAnimTrace),
                             std::span<const Site>(outfit_sync_sites::kLookSync), std::span<const Site>(outfit_sync_sites::kItemWear),
                             std::span<const Site>(outfit_sync_sites::kWeaponSlots), std::span<const Site>(outfit_sync_sites::kTraces)}) {
        for (const Site& site : table) others.push_back({site.address, sizeOf(site)});
    }
    for (const session_kick_sites::Set& set : session_kick_sites::kSets) {
        for (const Site& site : set.sites) others.push_back({site.address, sizeOf(site)});
    }
    for (const player_sites::SkinPoolSite& site : player_sites::kSkinPool) others.push_back({site.address, partner_sites::parseHex(site.expected).size()});
    return others;
}

void theClearance() {
    const std::vector<Site> all = everySite();
    bool apart = true;
    for (const Site& a : all) {
        for (const Site& b : all) {
            if (&a != &b && other_sites::overlaps(a.address, sizeOf(a), b.address, sizeOf(b))) apart = false;
        }
    }
    check(apart, "no two rows overlap");

    const std::vector<other_sites::Range> others = earlierRanges();
    bool clear = true;
    bool clearOfJuiced = true;
    bool clearOfOperands = true;
    for (const Site& site : all) {
        if (other_sites::anyOverlap(others, site.address, sizeOf(site))) {
            std::printf("     %s at 0x%X overlaps a site of an earlier milestone\n", site.id, static_cast<unsigned>(site.address));
            clear = false;
        }
        const uintptr_t first = site.address - kClearance;
        const uintptr_t last = site.address + sizeOf(site) + kClearance;
        if (std::any_of(std::begin(juiced::kTargets), std::end(juiced::kTargets), [&](uint32_t target) { return target >= first && target < last; })) {
            std::printf("     %s at 0x%X is within %u bytes of a Juiced target\n", site.id, static_cast<unsigned>(site.address), static_cast<unsigned>(kClearance));
            clearOfJuiced = false;
        }
        if (std::any_of(std::begin(kJuicedMinimapOperands), std::end(kJuicedMinimapOperands),
                        [&](uintptr_t operand) { return operand + kDwordBytes > first && operand < last; })) {
            std::printf("     %s at 0x%X is within %u bytes of an operand Juiced rewrites\n", site.id, static_cast<unsigned>(site.address),
                        static_cast<unsigned>(kClearance));
            clearOfOperands = false;
        }
    }
    check(clear, "no row overlaps a site of another milestone");
    check(clearOfJuiced, "no row is within 16 bytes of a Juiced target");
    check(clearOfOperands, "no row is within 16 bytes of an operand Juiced's range scans rewrite");
}

}  // namespace

int main() {
    theTables();
    theShapes();
    theCallees();
    theResumeAddresses();
    theClearance();
    return g_failures == 0 ? 0 : 1;
}
