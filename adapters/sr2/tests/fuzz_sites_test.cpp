// The milestone 17 wave 3 site tables (no game): the row counts of every set, the shape of each kind, where the calls and the
// jumps of the written bytes land, the FZ20 block against its two landing rows, and clearance from every other milestone's writes
// and from everything Juiced touches.
#include <algorithm>
#include <cstdio>
#include <set>
#include <span>
#include <string_view>
#include <vector>

#include "../src/anim_stream_sites.h"
#include "../src/char_types_sites.h"
#include "../src/fuzz_sites.h"
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
#include "../src/store_sites.h"
#include "../src/trace_sites.h"
#include "../src/vehicle_pool_references.h"
#include "../src/vehicle_pool_sites.h"
#include "../src/vehicle_seating_sites.h"
#include "juiced_targets.h"
#include "other_sites.h"

namespace {

using partner_sites::Kind;
using partner_sites::Site;

constexpr uintptr_t kClearance = 16;
constexpr size_t kBranchBytes = 5;
constexpr size_t kPointerBytes = 4;
constexpr size_t kHookBytes = 8;
constexpr size_t kRiotBlockBytes = 0x4A;
constexpr uintptr_t kThunk = 0x10004000;
constexpr size_t kLongJumpIfZeroBytes = 6;  // je rel32
constexpr size_t kTestAlAlBytes = 2;
constexpr size_t kShortJumpBytes = 2;
constexpr uint8_t kNop = 0x90;
constexpr uint8_t kShortJump = 0xEB;
constexpr uint8_t kTestAl = 0x84;
constexpr uint8_t kTestAlOperand = 0xC0;
constexpr uint8_t kTwoByteOpcode = 0x0F;
constexpr uint8_t kJumpIfZeroLong = 0x84;

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_failures;
}

std::vector<Site> ourSites() {
    std::vector<Site> all;
    for (const fuzz_sites::Set& set : fuzz_sites::kSets) all.insert(all.end(), set.sites.begin(), set.sites.end());
    return all;
}

size_t sizeOf(const Site& site) { return partner_sites::parseHex(site.expected).size(); }

bool isCheck(const Site& site) { return site.kind == Kind::Bytes; }

const Site* siteNamed(const std::vector<Site>& sites, std::string_view id) {
    for (const Site& site : sites) {
        if (id == site.id) return &site;
    }
    return nullptr;
}

size_t setSize(std::string_view name) {
    for (const fuzz_sites::Set& set : fuzz_sites::kSets) {
        if (name == set.name) return set.sites.size();
    }
    return 0;
}

uint32_t dwordOf(const std::vector<uint8_t>& bytes, size_t at) {
    return bytes[at] | bytes[at + 1] << 8 | bytes[at + 2] << 16 | static_cast<uint32_t>(bytes[at + 3]) << 24;
}

// Where the call of a retargeted call site goes.
uintptr_t callTarget(const Site& site) { return site.address + kBranchBytes + dwordOf(partner_sites::parseHex(site.expected), 1); }

void theRows() {
    const std::vector<Site> sites = ourSites();
    check(sites.size() == 22, "22 rows");
    check(std::size(fuzz_sites::kSets) == 7, "seven sets");
    check(setSize("level") == 3 && setSize("weapons") == 7 && setSize("crime area") == 1 && setSize("cameraman") == 2 && setSize("targets") == 2 &&
              setSize("vehicle") == 1 && setSize("riot") == 6,
          "row counts: level 3, weapons 7, crime area 1, cameraman 2, targets 2, vehicle 1, riot 6");
    const size_t written = static_cast<size_t>(std::count_if(sites.begin(), sites.end(), [](const Site& site) { return !isCheck(site); }));
    check(written == 20, "20 written sites, 2 checked rows");
    check(std::all_of(sites.begin(), sites.end(), [](const Site& site) { return site.gate == partner_sites::Gate::Now; }), "every row is written now");

    std::set<std::string_view> ids;
    std::set<uintptr_t> addresses;
    bool unique = true;
    for (const Site& site : sites) unique = unique && ids.insert(site.id).second && addresses.insert(site.address).second;
    check(unique, "ids and addresses are unique");

    bool apart = true;
    for (const Site& a : sites) {
        for (const Site& b : sites) {
            if (&a != &b && other_sites::overlaps(a.address, sizeOf(a), b.address, sizeOf(b))) {
                std::printf("     %s overlaps %s\n", a.id, b.id);
                apart = false;
            }
        }
    }
    check(apart, "no two rows overlap");
}

void theShapes() {
    const std::vector<Site> sites = ourSites();
    bool fit = true;
    for (const Site& site : sites) {
        const auto bytes = partner_sites::parseHex(site.expected);
        if (site.kind == Kind::RetargetCall && (bytes.size() != kBranchBytes || bytes[0] != 0xE8 || !site.tail.empty())) fit = false;
        if (site.kind == Kind::HookEntry && (bytes.size() != kHookBytes || !site.tail.empty())) fit = false;
        if (site.kind == Kind::CallBlock && bytes.size() != kBranchBytes + partner_sites::parseHex(site.tail).size()) fit = false;
        if (site.kind == Kind::Bytes && (!site.tail.empty() || site.thunk != partner_sites::Thunk::None)) fit = false;
        if (site.kind == Kind::JumpOut || site.kind == Kind::PointerSwap) fit = false;
    }
    check(fit, "each row has the shape of its kind: a 5 byte call, an 8 byte hook, a call block whose tail fills the rest, a check with no tail and no thunk");
    bool lengths = true;
    for (const Site& site : sites) {
        if (site.kind != Kind::HookEntry && !isCheck(site)) lengths = lengths && partner_replacement::replacementFor(site, kThunk).size() == sizeOf(site);
    }
    check(lengths, "each written row replaces exactly the bytes it checks");
    bool checksWriteNothing = true;
    for (const Site& site : sites) {
        if (isCheck(site)) checksWriteNothing = checksWriteNothing && partner_replacement::replacementFor(site, 0).empty();
    }
    check(checksWriteNothing, "the two check rows write nothing");
}

void theCallees() {
    const std::vector<Site> sites = ourSites();
    struct Callee {
        const char* id;
        uintptr_t function;
    };
    constexpr Callee kCallees[] = {
        {"FZ1", sr2::kDismissFollowers},   {"FZ2", sr2::kFuzzSetMode},         {"FZ3", sr2::kFuzzSetMode},         {"FZ4", sr2::kGiveWeapon},
        {"FZ5", sr2::kRemoveWeapon},       {"FZ6", sr2::kRemoveWeapon},        {"FZ7", sr2::kGiveWeapon},          {"FZ8", sr2::kRemotePlayerGetter},
        {"FZ9", sr2::kGivePickupWeapon},   {"FZ10", sr2::kRemoveWeapon},       {"FZ11", sr2::kGivePickupWeapon},   {"FZ12", sr2::kRemotePlayerGetter},
        {"FZ13", sr2::kHumanFighting},     {"FZ14", sr2::kRemotePlayerGetter}, {"FZ16", sr2::kRemotePlayerGetter}, {"FZ17", sr2::kVehicleHolds},
        {"FZ18", sr2::kRemotePlayerGetter},
    };
    bool callees = true;
    for (const Callee& callee : kCallees) {
        const Site* site = siteNamed(sites, callee.id);
        const bool ok = site && site->kind == Kind::RetargetCall && callTarget(*site) == callee.function;
        if (!ok) std::printf("     %s does not replace the call of 0x%X\n", callee.id, static_cast<unsigned>(callee.function));
        callees = callees && ok;
    }
    check(callees, "every retargeted row replaces the call of the function the spec names");
    const size_t retargets = static_cast<size_t>(std::count_if(sites.begin(), sites.end(), [](const Site& site) { return site.kind == Kind::RetargetCall; }));
    check(retargets == std::size(kCallees), "all 17 retargeted rows are covered");
}

void theRiotBlock() {
    const std::vector<Site> sites = ourSites();
    const Site* block = siteNamed(sites, "FZ20");
    const Site* joins = siteNamed(sites, "FZc1");
    const Site* skips = siteNamed(sites, "FZc2");
    check(block && sizeOf(*block) == kRiotBlockBytes && block->kind == Kind::CallBlock, "FZ20 replaces the 0x4A bytes of the partner half of the rioter test");
    check(joins && skips && joins->address == sr2::kFuzzRioterJoins && skips->address == sr2::kFuzzRioterSkips, "FZc1 and FZc2 are the two landing addresses");
    const std::vector<uint8_t> written = partner_replacement::replacementFor(*block, kThunk);
    const size_t test = kBranchBytes;
    const size_t je = test + kTestAlAlBytes;
    const size_t jmp = je + kLongJumpIfZeroBytes;
    check(written[test] == kTestAl && written[test + 1] == kTestAlOperand, "FZ20: test al, al after the call");
    const uintptr_t jeEnd = block->address + je + kLongJumpIfZeroBytes;
    check(written[je] == kTwoByteOpcode && written[je + 1] == kJumpIfZeroLong && jeEnd + dwordOf(written, je + 2) == skips->address,
          "FZ20: the je lands on the next human (FZc2)");
    check(written[jmp] == kShortJump && block->address + jmp + kShortJumpBytes + written[jmp + 1] == joins->address,
          "FZ20: the jmp lands where the rioter joins (FZc1)");
    check(std::all_of(written.begin() + static_cast<std::ptrdiff_t>(jmp + kShortJumpBytes), written.end(), [](uint8_t byte) { return byte == kNop; }),
          "FZ20: nops to the end of the block");
    check(block->address + sizeOf(*block) <= joins->address, "FZ20 ends before the joins row");
}

std::vector<other_sites::Range> everyEarlierWrite() {
    std::vector<other_sites::Range> others = other_sites::milestones3to8();
    const auto activities = other_sites::activities();
    const auto crowd = other_sites::players3and4();
    others.insert(others.end(), activities.begin(), activities.end());
    others.insert(others.end(), crowd.begin(), crowd.end());
    const auto add = [&others](const Site& site) { others.push_back({site.address, sizeOf(site)}); };
    const auto addAll = [&add](std::span<const Site> table) {
        for (const Site& site : table) add(site);
    };
    for (const auto table : multiplayer_sites::kScriptTraces) addAll(table);
    for (const auto table : {std::span<const Site>(trace_sites::kPlayers), std::span<const Site>(trace_sites::kCensus),
                             std::span<const Site>(trace_sites::kCutsceneTrace), std::span<const Site>(trace_sites::kScriptTrace),
                             std::span<const Site>(player_sites::kNetClock), std::span<const Site>(player_sites::kBundleSender),
                             std::span<const Site>(player_sites::kCutsceneVisuals), std::span<const Site>(player_sites::kAnimTrace),
                             std::span<const Site>(vehicle_pool_sites::kHeadroomBlocks), std::span<const Site>(outfit_sync_sites::kLookSync),
                             std::span<const Site>(outfit_sync_sites::kItemWear), std::span<const Site>(outfit_sync_sites::kWeaponSlots),
                             std::span<const Site>(outfit_sync_sites::kTraces), std::span<const Site>(revive_sites::kRevive),
                             std::span<const Site>(revive_sites::kNotoriety), std::span<const Site>(mission_flow_sites::kPrompts),
                             std::span<const Site>(mission_flow_sites::kReplay), std::span<const Site>(mission_flow_sites::kRewards),
                             std::span<const Site>(mission_flow_sites::kTrace), std::span<const Site>(vehicle_seating_sites::kTrafficking),
                             std::span<const Site>(vehicle_seating_sites::kCheckpoint), std::span<const Site>(vehicle_seating_sites::kMissionExit)}) {
        addAll(table);
    }
    for (const auto& site : player_sites::kSkinPool) others.push_back({site.address, partner_sites::parseHex(site.expected).size()});
    for (const auto& reference : vehicle_pool_references::kReferences) others.push_back({reference.site, kPointerBytes});
    for (const auto& bound : vehicle_pool_sites::kBounds) others.push_back({bound.address, partner_sites::parseHex(bound.expected).size()});
    for (const anim_stream_sites::Phase& phase : anim_stream_sites::kPhases) {
        addAll(phase.sites);
        for (const anim_stream_sites::NeedTableSite& site : phase.immediates) others.push_back({site.address, partner_sites::parseHex(site.expected).size()});
    }
    for (const session_kick_sites::Set& set : session_kick_sites::kSets) addAll(set.sites);
    for (const net_handlers_sites::Set& set : net_handlers_sites::kSets) addAll(set.sites);
    for (const hud_sites::Set& set : hud_sites::kSets) addAll(set.sites);
    for (const party_ai_sites::Phase& phase : party_ai_sites::kPhases) addAll(phase.sites);
    for (const store_sites::Set& set : store_sites::kSets) addAll(set.sites);
    for (const char_types_sites::Row& row : char_types_sites::kRows) others.push_back({row.address, partner_sites::parseHex(row.expected).size()});
    return others;
}

void theClearance() {
    const std::vector<other_sites::Range> others = everyEarlierWrite();
    bool clear = true;
    bool clearOfJuiced = true;
    for (const Site& site : ourSites()) {
        if (other_sites::anyOverlap(others, site.address - kClearance, sizeOf(site) + 2 * kClearance)) {
            std::printf("     %s at 0x%X is within %u bytes of a site of an earlier milestone\n", site.id, static_cast<unsigned>(site.address),
                        static_cast<unsigned>(kClearance));
            clear = false;
        }
        const uintptr_t first = site.address - kClearance;
        const uintptr_t last = site.address + sizeOf(site) + kClearance;
        if (std::any_of(std::begin(juiced::kTargets), std::end(juiced::kTargets), [&](uint32_t target) { return target >= first && target < last; })) {
            std::printf("     %s at 0x%X is within %u bytes of a Juiced target\n", site.id, static_cast<unsigned>(site.address), static_cast<unsigned>(kClearance));
            clearOfJuiced = false;
        }
    }
    check(clear, "every row is 16 bytes clear of a write of the earlier milestones");
    check(clearOfJuiced, "every row is 16 bytes clear of an address Juiced touches");
}

}  // namespace

int main() {
    theRows();
    theShapes();
    theCallees();
    theRiotBlock();
    theClearance();
    return g_failures == 0 ? 0 : 1;
}
