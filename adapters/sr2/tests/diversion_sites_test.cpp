// The milestone 16 wave 2 site tables (no game): the row counts of every set, the shapes of each kind, where the calls and
// the jumps of the written bytes land, and clearance from every other milestone's writes and from everything Juiced touches.
#include <algorithm>
#include <cstdio>
#include <set>
#include <span>
#include <string_view>
#include <vector>

#include "../src/activity_sites.h"
#include "../src/anim_stream_sites.h"
#include "../src/diversion_sites.h"
#include "../src/multiplayer_sites.h"
#include "../src/outfit_sync_sites.h"
#include "../src/partner_replacement.h"
#include "../src/player_sites.h"
#include "../src/session_kick_sites.h"
#include "../src/vehicle_pool_references.h"
#include "../src/vehicle_pool_sites.h"
#include "juiced_targets.h"
#include "other_sites.h"

namespace {

using partner_sites::Gate;
using partner_sites::Kind;
using partner_sites::Site;

constexpr uintptr_t kJuicedMargin = 16;
constexpr size_t kBranchBytes = 5;
constexpr uintptr_t kFraudZoneTail = 0x64A0C3;   // pop ebx, pop esi, ret of the zone test
constexpr uintptr_t kCarryOtherTail = 0x656D7F;  // what the carried count function does for any other human
constexpr uint8_t kShortJumpIfZero = 0x74;
constexpr uint8_t kShortJump = 0xEB;
constexpr uintptr_t kShortJumpBytes = 2;

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_failures;
}

size_t sizeOf(const Site& site) { return partner_sites::parseHex(site.expected).size(); }

std::vector<Site> allSites() {
    std::vector<Site> all;
    for (const diversion_sites::Set& set : diversion_sites::kSets) all.insert(all.end(), set.sites.begin(), set.sites.end());
    return all;
}

const Site* siteNamed(const std::vector<Site>& sites, std::string_view id) {
    for (const Site& site : sites) {
        if (id == site.id) return &site;
    }
    return nullptr;
}

uint32_t dwordOf(const std::vector<uint8_t>& bytes, size_t at) {
    return bytes[at] | bytes[at + 1] << 8 | bytes[at + 2] << 16 | static_cast<uint32_t>(bytes[at + 3]) << 24;
}

// Where the `call rel32` of a retargeted call site goes.
uintptr_t callTarget(const Site& site) { return site.address + kBranchBytes + dwordOf(partner_sites::parseHex(site.expected), 1); }

uintptr_t shortJumpTarget(uintptr_t at, uint8_t displacement) { return at + kShortJumpBytes + displacement; }

void theCounts() {
    struct Count {
        const char* set;
        size_t rows;
    };
    constexpr Count kCounts[] = {{"streaking", 5}, {"tow", 1},    {"fraud", 14},   {"snatch", 12},
                                 {"trafficking", 6}, {"mayhem", 9}, {"district", 6}, {"phase B", 4}};
    bool same = std::size(kCounts) == std::size(diversion_sites::kSets);
    for (size_t i = 0; same && i < std::size(kCounts); ++i) {
        same = std::string_view(diversion_sites::kSets[i].name) == kCounts[i].set && diversion_sites::kSets[i].sites.size() == kCounts[i].rows;
    }
    check(same, "the sets: streaking 5, tow 1, fraud 14, snatch 12, trafficking 6, mayhem 9, district 6, phase B 4");
    check(allSites().size() == 57, "57 rows here; with AC46 (DVP9a) of the activity table the 58 of the spec");
    bool onlyPhaseBWaits = true;
    for (const diversion_sites::Set& set : diversion_sites::kSets) {
        for (const Site& site : set.sites) {
            onlyPhaseBWaits = onlyPhaseBWaits && (site.gate == Gate::PhaseB) == (std::string_view(set.name) == "phase B");
        }
    }
    check(onlyPhaseBWaits, "only phase B waits for partner_experimental");
}

void theShapes() {
    bool fit = true;
    bool sameLength = true;
    for (const Site& site : allSites()) {
        const auto bytes = partner_sites::parseHex(site.expected);
        if (site.kind == Kind::RetargetCall && (bytes.size() != kBranchBytes || bytes[0] != 0xE8 || !site.tail.empty())) fit = false;
        if (site.kind == Kind::HookEntry && (bytes.size() < kBranchBytes || !site.tail.empty())) fit = false;
        if (site.kind == Kind::JumpOut && (bytes.size() < kBranchBytes || !site.tail.empty())) fit = false;
        if (site.kind == Kind::CallBlock && bytes.size() != kBranchBytes + partner_sites::parseHex(site.tail).size()) fit = false;
        if (site.kind != Kind::HookEntry) sameLength = sameLength && partner_replacement::replacementFor(site, 0x10000000).size() == bytes.size();
    }
    check(fit, "each row has the shape of its kind: a 5 byte call, a hook with room for a branch, a block whose tail fills the rest");
    check(sameLength, "each written row replaces exactly the bytes it checks");
}

void theWrittenJumps() {
    const std::vector<Site> sites = allSites();
    struct Resume {
        const char* id;
        uintptr_t address;
    };
    constexpr Resume kResumes[] = {{"DVP3", 0x656C43}, {"DVP4b", 0x65DA35}, {"DVP8", 0x65AF50},
                                   {"DVK6", 0x666D80}, {"DVM3", 0x66864E},  {"DVM6", 0x66C050}};
    bool resumes = true;
    for (const Resume& resume : kResumes) {
        const Site* site = siteNamed(sites, resume.id);
        resumes = resumes && site && site->kind == Kind::JumpOut && site->address + sizeOf(*site) == resume.address;
    }
    check(resumes, "each jump out replaces the block that ends at its resume address (section 4)");
    const Site* seats = siteNamed(sites, "DVK7");
    check(seats && seats->kind == Kind::JumpOut && sizeOf(*seats) == 8, "DVK7 replaces the 8 byte epilogue it repeats");

    const Site* zone = siteNamed(sites, "DVF1");
    const auto zoneTail = partner_replacement::replacementFor(*zone, 0x10000000);
    check(zoneTail[kBranchBytes] == kShortJump && shortJumpTarget(zone->address + kBranchBytes, zoneTail[kBranchBytes + 1]) == kFraudZoneTail,
          "DVF1: the thunk's answer goes to the pop ebx, pop esi, ret at 0x64A0C3");
    const Site* coat = siteNamed(sites, "DVS6");
    const auto coatTail = partner_replacement::replacementFor(*coat, 0x10000000);
    check(coatTail[kBranchBytes] == kShortJump &&
              shortJumpTarget(coat->address + kBranchBytes, coatTail[kBranchBytes + 1]) == coat->address + sizeOf(*coat),
          "DVS6: the jump skips the nops to the end of the replaced block");

    const Site* carry = nullptr;
    for (const Site& site : activity_sites::kPhase2) {
        if (std::string_view(site.id) == "AC46") carry = &site;
    }
    check(carry && carry->kind == Kind::CallBlock && carry->thunk == partner_sites::Thunk::SnatchCarryOfEsi, "AC46 (DVP9a) is a call block of the carry thunk");
    const auto carryTail = partner_replacement::replacementFor(*carry, 0x10000000);
    check(carryTail[kBranchBytes] == kShortJumpIfZero &&
              shortJumpTarget(carry->address + kBranchBytes, carryTail[kBranchBytes + 1]) == kCarryOtherTail && carryTail[kBranchBytes + 2] == 0xC2 &&
              carryTail[kBranchBytes + 3] == 4 && carryTail[kBranchBytes + 4] == 0,
          "AC46: jz to 0x656D7F for another human, else ret 4 with the count in eax");
}

void theCalls() {
    const std::vector<Site> sites = allSites();
    struct Callee {
        const char* id;
        uintptr_t function;
    };
    constexpr Callee kCallees[] = {
        {"DVK2", sr2::kDismissFollowers}, {"DVK3", sr2::kTraffickingArm},      {"DVK4", sr2::kTraffickingDisarm},
        {"DVM1", sr2::kMayhemArm},        {"DVM2", sr2::kMayhemDisarm},        {"DVM5", sr2::kMayhemComplication},
        {"DVK5", sr2::kTeleportHuman},
    };
    bool callees = true;
    for (const Callee& callee : kCallees) {
        const Site* site = siteNamed(sites, callee.id);
        callees = callees && site && callTarget(*site) == callee.function;
    }
    check(callees, "the each-remote rows replace the call of the per-player function they name");
    bool getters = true;
    for (const Site& site : sites) {
        if (site.kind != Kind::RetargetCall) continue;
        if (std::any_of(std::begin(kCallees), std::end(kCallees), [&](const Callee& callee) { return std::string_view(callee.id) == site.id; })) continue;
        if (callTarget(site) != sr2::kRemotePlayerGetter) {
            std::printf("     %s calls 0x%X, not the partner getter\n", site.id, static_cast<unsigned>(callTarget(site)));
            getters = false;
        }
    }
    check(getters, "every other retargeted call is a call of the partner getter 0x9D7BD0");
    const Site* firstBlockCall = siteNamed(sites, "DVF1");
    check(callTarget(*firstBlockCall) == sr2::kRemotePlayerGetter && callTarget(*siteNamed(sites, "DVF2")) == sr2::kRemotePlayerGetter,
          "the blocks of DVF1 and DVF2 start with the partner getter call");
}

// Everything the milestones before this one write: the partner, activity (AC46 apart: it is DVP9a), players 3 and 4, anim
// streaming, outfit sync and kick tables, the skin pool and the vehicle pool.
std::vector<other_sites::Range> earlierWrites() {
    std::vector<other_sites::Range> others = other_sites::milestones3to8();
    const auto crowd = other_sites::players3and4();
    others.insert(others.end(), crowd.begin(), crowd.end());
    auto add = [&](const Site& site) { others.push_back({site.address, sizeOf(site)}); };
    for (const auto phase : {std::span<const Site>(activity_sites::kPhase1), std::span<const Site>(activity_sites::kPhase2),
                             std::span<const Site>(activity_sites::kPhase3)}) {
        for (const Site& site : phase) {
            if (std::string_view(site.id) != "AC46") add(site);
        }
    }
    for (const anim_stream_sites::Phase& phase : anim_stream_sites::kPhases) {
        for (const Site& site : phase.sites) add(site);
    }
    for (const auto& set : session_kick_sites::kSets) {
        for (const Site& site : set.sites) add(site);
    }
    for (const auto table : {std::span<const Site>(outfit_sync_sites::kLookSync), std::span<const Site>(outfit_sync_sites::kItemWear),
                             std::span<const Site>(outfit_sync_sites::kWeaponSlots), std::span<const Site>(outfit_sync_sites::kTraces)}) {
        for (const Site& site : table) add(site);
    }
    for (const auto table : multiplayer_sites::kScriptTraces) {
        for (const Site& site : table) add(site);
    }
    for (const auto& site : player_sites::kSkinPool) others.push_back({site.address, partner_sites::parseHex(site.expected).size()});
    for (const auto& reference : vehicle_pool_references::kReferences) others.push_back({reference.site, sizeof(uint32_t)});
    for (const auto& bound : vehicle_pool_sites::kBounds) others.push_back({bound.address, partner_sites::parseHex(bound.expected).size()});
    for (const Site& site : vehicle_pool_sites::kHeadroomBlocks) add(site);
    return others;
}

void theClearance() {
    const std::vector<Site> sites = allSites();
    std::set<std::string_view> ids;
    std::set<uintptr_t> addresses;
    bool unique = true;
    bool apart = true;
    for (const Site& a : sites) {
        unique = unique && ids.insert(a.id).second && addresses.insert(a.address).second;
        for (const Site& b : sites) apart = apart && (&a == &b || !other_sites::overlaps(a.address, sizeOf(a), b.address, sizeOf(b)));
    }
    check(unique, "ids and addresses are unique");
    check(apart, "no two rows overlap");
    const std::vector<other_sites::Range> others = earlierWrites();
    bool clear = true;
    bool clearOfJuiced = true;
    for (const Site& site : sites) {
        if (other_sites::anyOverlap(others, site.address - kJuicedMargin, sizeOf(site) + 2 * kJuicedMargin)) {
            std::printf("     %s at 0x%X is within %u bytes of another milestone's write\n", site.id, static_cast<unsigned>(site.address),
                        static_cast<unsigned>(kJuicedMargin));
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
    check(clear, "no row within 16 bytes of a write of milestones 3 to 15");
    check(clearOfJuiced, "no row within 16 bytes of an address Juiced touches");
}

}  // namespace

int main() {
    theCounts();
    theShapes();
    theWrittenJumps();
    theCalls();
    theClearance();
    return g_failures == 0 ? 0 : 1;
}
