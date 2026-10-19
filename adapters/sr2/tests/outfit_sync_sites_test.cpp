// The milestone 16 W1 site tables (no game): ids and addresses, shapes, the callee of each retargeted call, and clearance from every
// other site and from everything Juiced touches.
#include <algorithm>
#include <cstdio>
#include <set>
#include <span>
#include <string_view>
#include <vector>

#include "../src/multiplayer_sites.h"
#include "../src/outfit_sync_sites.h"
#include "../src/partner_replacement.h"
#include "../src/sr2_addresses.h"
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
constexpr uintptr_t kThunk = 0x10004000;
constexpr uint8_t kCallOpcode = 0xE8;
constexpr size_t kWrittenSites = 6;
constexpr size_t kTraceSites = 3;

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_failures;
}

std::vector<Site> writtenSites() {
    std::vector<Site> all;
    for (const auto table : {std::span<const Site>(outfit_sync_sites::kLookSync), std::span<const Site>(outfit_sync_sites::kItemWear),
                             std::span<const Site>(outfit_sync_sites::kWeaponSlots)}) {
        all.insert(all.end(), table.begin(), table.end());
    }
    return all;
}

std::vector<Site> everySite() {
    std::vector<Site> all = writtenSites();
    all.insert(all.end(), std::begin(outfit_sync_sites::kTraces), std::end(outfit_sync_sites::kTraces));
    return all;
}

size_t sizeOf(const Site& site) { return partner_sites::parseHex(site.expected).size(); }

const Site* siteNamed(const std::vector<Site>& sites, std::string_view id) {
    for (const Site& site : sites) {
        if (id == site.id) return &site;
    }
    return nullptr;
}

// Where the `call rel32` of a retargeted call site goes.
uintptr_t callTarget(const Site& site) {
    const auto bytes = partner_sites::parseHex(site.expected);
    const uint32_t displacement = bytes[1] | bytes[2] << 8 | bytes[3] << 16 | static_cast<uint32_t>(bytes[4]) << 24;
    return site.address + kBranchBytes + displacement;
}

bool idsAndAddressesAreUnique(const std::vector<Site>& sites) {
    std::set<std::string_view> ids;
    std::set<uintptr_t> addresses;
    for (const Site& site : sites) {
        if (!ids.insert(site.id).second || !addresses.insert(site.address).second) return false;
    }
    return true;
}

void theTables() {
    const std::vector<Site> written = writtenSites();
    const std::vector<Site> all = everySite();
    check(written.size() == kWrittenSites && std::size(outfit_sync_sites::kTraces) == kTraceSites, "6 written sites and 3 trace sites");
    check(idsAndAddressesAreUnique(all), "ids and addresses are unique");
    check(std::size(outfit_sync_sites::kLookSync) == 2 && std::size(outfit_sync_sites::kItemWear) == 2 && std::size(outfit_sync_sites::kWeaponSlots) == 2,
          "three sets of two");
    check(std::all_of(all.begin(), all.end(), [](const Site& site) { return site.tail.empty(); }),
          "every site is written when its set is (no tail)");
    check(std::all_of(all.begin(), all.end(), [](const Site& site) { return site.thunk != Thunk::None; }), "every site has a thunk");
    check(std::all_of(std::begin(outfit_sync_sites::kLookSync), std::end(outfit_sync_sites::kLookSync),
                      [](const Site& site) { return site.thunk == Thunk::EachRemoteMember82BC50; }),
          "set A reuses the existing look sync thunk");
}

void theShapes() {
    const std::vector<Site> written = writtenSites();
    bool calls = true;
    for (const Site& site : written) {
        const auto bytes = partner_sites::parseHex(site.expected);
        calls = calls && site.kind == Kind::RetargetCall && bytes.size() == kBranchBytes && bytes[0] == kCallOpcode;
    }
    check(calls, "every written site is a 5 byte call retargeted");
    bool hooks = true;
    for (const Site& site : outfit_sync_sites::kTraces) hooks = hooks && site.kind == Kind::HookEntry && sizeOf(site) >= kBranchBytes;
    check(hooks, "every trace site is a hook over at least 5 bytes");
    bool replaced = true;
    for (const Site& site : written) {
        const auto bytes = partner_replacement::replacementFor(site, kThunk);
        replaced = replaced && bytes.size() == kBranchBytes && bytes[0] == kCallOpcode;
    }
    check(replaced, "each retargeted call keeps its opcode and length");
}

void theCallees() {
    const std::vector<Site> all = everySite();
    const auto callee = [&](std::string_view id) { return callTarget(*siteNamed(all, id)); };
    check(callee("OF1") == sr2::kItemApply, "OF1 calls the item applier 0x53FBB0");
    check(callee("OF2") == sr2::kSendBroadcast, "OF2 calls the broadcast 0x814740");
    check(callee("OF3") == sr2::kWeaponSlotsApply, "OF3 calls the weapon slot applier 0x796330");
    check(callee("OF4") == sr2::kSendBroadcast, "OF4 calls the broadcast 0x814740");
    check(callee("OF5") == sr2::kLookSyncB && callee("OF6") == sr2::kLookSyncB, "OF5 and OF6 call look sync B 0x82BC50");
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
    for (const auto& row : vehicle_pool_references::kReferences) others.push_back({row.site, kDwordBytes});
    for (const auto& bound : vehicle_pool_sites::kBounds) others.push_back({bound.address, partner_sites::parseHex(bound.expected).size()});
    for (const Site& site : vehicle_pool_sites::kHeadroomBlocks) others.push_back({site.address, sizeOf(site)});
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
    check(apart, "no two sites overlap");

    const std::vector<other_sites::Range> others = earlierRanges();
    bool clear = true;
    bool clearOfJuiced = true;
    for (const Site& site : all) {
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
    check(clear, "no site overlaps a site of milestones 3 to 15");
    check(clearOfJuiced, "no site is within 16 bytes of a Juiced target");
}

}  // namespace

int main() {
    theTables();
    theShapes();
    theCallees();
    theClearance();
    return g_failures == 0 ? 0 : 1;
}
