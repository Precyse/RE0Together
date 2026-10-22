// The milestone 16 W2 vehicle seating site tables (no game): ids and addresses, shapes, the callee and the branch targets of each site,
// and clearance from every other site and from everything Juiced touches.
#include <algorithm>
#include <cstdio>
#include <set>
#include <span>
#include <string_view>
#include <vector>

#include "../src/multiplayer_sites.h"
#include "../src/partner_replacement.h"
#include "../src/sr2_addresses.h"
#include "../src/vehicle_pool_references.h"
#include "../src/vehicle_pool_sites.h"
#include "../src/vehicle_seating_sites.h"
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
constexpr size_t kSe1Bytes = 16;
constexpr size_t kShortBranchBytes = 2;
constexpr size_t kTestAlBytes = 2;  // the `test al, al` before the jnz of the SE1 tail
constexpr uintptr_t kThunk = 0x10004000;
constexpr uint8_t kCallOpcode = 0xE8;
constexpr uint8_t kJmpOpcode = 0xE9;
constexpr uint8_t kJnzOpcode = 0x75;
constexpr uint8_t kJeOpcode = 0x74;
constexpr uintptr_t kSe1Join = 0x9D8769;  // SE1: a remote's car goes on here, with the seat fixed
constexpr size_t kWrittenSites = 4;

std::vector<Site> writtenSites() {
    std::vector<Site> all;
    for (const auto table : {std::span<const Site>(vehicle_seating_sites::kTrafficking), std::span<const Site>(vehicle_seating_sites::kCheckpoint),
                             std::span<const Site>(vehicle_seating_sites::kMissionExit)}) {
        all.insert(all.end(), table.begin(), table.end());
    }
    return all;
}

const Site& siteNamed(const std::vector<Site>& sites, std::string_view id) {
    return *std::find_if(sites.begin(), sites.end(), [&](const Site& site) { return id == site.id; });
}

void theTables() {
    const std::vector<Site> all = writtenSites();
    check(writtenSites().size() == kWrittenSites, "4 written sites");
    check(idsAndAddressesAreUnique(all), "ids and addresses are unique");
    check(std::size(vehicle_seating_sites::kTrafficking) == 1 && std::size(vehicle_seating_sites::kCheckpoint) == 1 &&
              std::size(vehicle_seating_sites::kMissionExit) == 2,
          "SE1 alone, SE2 alone, SE3 and SE4 together");
    check(std::all_of(all.begin(), all.end(), [](const Site& site) { return site.thunk != Thunk::None; }), "every site has a thunk");
}

void theShapes() {
    const std::vector<Site> all = writtenSites();
    const Site& se1 = siteNamed(all, "SE1");
    const auto se1Bytes = partner_replacement::replacementFor(se1, kThunk);
    check(se1.kind == Kind::CallBlock && sizeOf(se1) == kSe1Bytes && se1Bytes.size() == kSe1Bytes && se1Bytes[0] == kCallOpcode,
          "SE1 is a call block of 16 bytes and its replacement is 16 bytes");
    const size_t jnz = kBranchBytes + kTestAlBytes;
    check(se1Bytes[jnz] == kJnzOpcode && se1.address + jnz + kShortBranchBytes + se1Bytes[jnz + 1] == kSe1Join,
          "the jnz of the SE1 tail lands on 0x9D8769");
    check(std::all_of(se1Bytes.begin() + jnz + kShortBranchBytes, se1Bytes.end(), [](uint8_t byte) { return byte == kNop; }),
          "the rest of the SE1 tail is nops that run on into 0x9D8761");

    const Site& se2 = siteNamed(all, "SE2");
    check(se2.kind == Kind::RetargetCall && sizeOf(se2) == kBranchBytes && partner_sites::parseHex(se2.expected)[0] == kCallOpcode,
          "SE2 is a 5 byte call retargeted");
    const Site& se3 = siteNamed(all, "SE3");
    const auto se3Bytes = partner_replacement::replacementFor(se3, kThunk);
    check(se3.kind == Kind::JumpOut && sizeOf(se3) >= kBranchBytes && se3Bytes.size() == sizeOf(se3) && se3Bytes[0] == kJmpOpcode,
          "SE3 is a jump out over at least 5 bytes");
    const Site& se4 = siteNamed(all, "SE4");
    check(se4.kind == Kind::RetargetCall && sizeOf(se4) == kBranchBytes && partner_sites::parseHex(se4.expected)[0] == kCallOpcode,
          "SE4 is a 5 byte call retargeted");
}

void theTargets() {
    const std::vector<Site> all = writtenSites();
    check(callTarget(siteNamed(all, "SE1")) == sr2::kRemotePlayerGetter, "SE1 replaces a call of the partner getter 0x9D7BD0");
    check(callTarget(siteNamed(all, "SE2")) == sr2::kRemotePlayerGetter, "SE2 replaces a call of the partner getter 0x9D7BD0");
    check(callTarget(siteNamed(all, "SE3")) == sr2::kRemotePlayerGetter, "SE3 replaces a call of the partner getter 0x9D7BD0");
    check(callTarget(siteNamed(all, "SE4")) == sr2::kReleaseHeld, "SE4 replaces a call of the release 0x69DDA0");

    const Site& se3 = siteNamed(all, "SE3");
    const auto bytes = partner_sites::parseHex(se3.expected);
    const size_t je = bytes.size() - kShortBranchBytes;
    check(bytes[je] == kJeOpcode && se3.address + bytes.size() + bytes[je + 1] == sr2::kMissionExitPartnerDone,
          "the je SE3 replaces went to 0x6A4E85, where the thunk resumes");
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
    const std::vector<Site> all = writtenSites();
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
    theTargets();
    theClearance();
    return g_failures == 0 ? 0 : 1;
}
