// The milestone 7 site table (no game): counts per phase, shapes, overlaps with every other milestone's sites, the written
// bytes of the call block, and distance from everything Juiced touches.
#include <algorithm>
#include <cstdio>
#include <set>
#include <span>
#include <string_view>
#include <vector>

#include "../src/activity_sites.h"
#include "../src/partner_replacement.h"
#include "juiced_targets.h"
#include "other_sites.h"

namespace {

using activity_sites::Kind;
using activity_sites::Site;

constexpr size_t kPhase1Sites = 28;
constexpr size_t kPhase2Sites = 51;
constexpr size_t kPhase3Sites = 6;
constexpr uintptr_t kJuicedMargin = 16;
constexpr size_t kBranchBytes = 5;

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_failures;
}

std::vector<Site> allSites() {
    std::vector<Site> all;
    for (const auto phase : {std::span<const Site>(activity_sites::kPhase1), std::span<const Site>(activity_sites::kPhase2),
                             std::span<const Site>(activity_sites::kPhase3)}) {
        all.insert(all.end(), phase.begin(), phase.end());
    }
    return all;
}

size_t sizeOf(const Site& site) { return partner_sites::parseHex(site.expected).size(); }

using other_sites::overlaps;

bool idsAndAddressesAreUnique(const std::vector<Site>& sites) {
    std::set<std::string_view> ids;
    std::set<uintptr_t> addresses;
    for (const Site& site : sites) {
        if (!ids.insert(site.id).second || !addresses.insert(site.address).second) return false;
    }
    return true;
}

bool sitesDoNotOverlap(const std::vector<Site>& sites) {
    for (const Site& a : sites) {
        for (const Site& b : sites) {
            if (&a != &b && overlaps(a.address, sizeOf(a), b.address, sizeOf(b))) return false;
        }
    }
    return true;
}

bool clearOfEarlierMilestones(const std::vector<Site>& sites) {
    const auto others = other_sites::milestones3to8();
    for (const Site& site : sites) {
        if (other_sites::anyOverlap(others, site.address, sizeOf(site))) {
            std::printf("     %s at 0x%X overlaps an earlier milestone's site\n", site.id, static_cast<unsigned>(site.address));
            return false;
        }
    }
    return true;
}

bool clearOfJuiced(const std::vector<Site>& sites) {
    for (const Site& site : sites) {
        const uintptr_t first = site.address - kJuicedMargin;
        const uintptr_t last = site.address + sizeOf(site) + kJuicedMargin;
        if (std::any_of(std::begin(juiced::kTargets), std::end(juiced::kTargets),
                        [&](uint32_t target) { return target >= first && target < last; })) {
            std::printf("     %s at 0x%X is within %u bytes of a Juiced target\n", site.id, static_cast<unsigned>(site.address),
                        static_cast<unsigned>(kJuicedMargin));
            return false;
        }
    }
    return true;
}

bool shapesFit(const std::vector<Site>& sites) {
    for (const Site& site : sites) {
        const auto bytes = partner_sites::parseHex(site.expected);
        if (site.kind == Kind::RetargetCall && (bytes.size() != kBranchBytes || bytes[0] != 0xE8)) return false;
        if (site.kind == Kind::HookEntry && bytes.size() < kBranchBytes) return false;
        if (site.kind == Kind::CallBlock && bytes.size() < kBranchBytes + partner_sites::parseHex(site.tail).size()) return false;
    }
    return true;
}

const Site* siteNamed(const std::vector<Site>& sites, std::string_view id) {
    for (const Site& site : sites) {
        if (id == site.id) return &site;
    }
    return nullptr;
}

}  // namespace

int main() {
    const std::vector<Site> sites = allSites();
    check(std::span<const Site>(activity_sites::kPhase1).size() == kPhase1Sites,
          "phase 1: 28 sites (AF5-AF8, AV1, AV4-AV7, AV16, AC1-AC14, AD1-AD4)");
    check(std::span<const Site>(activity_sites::kPhase2).size() == kPhase2Sites,
          "phase 2: 51 sites (AV2, AV3, AV8-AV15, AC15-AC53, AD5, AD6)");
    check(std::span<const Site>(activity_sites::kPhase3).size() == kPhase3Sites, "phase 3: 6 sites (AD7-AD11, AF9)");
    check(idsAndAddressesAreUnique(sites), "ids and addresses are unique");
    check(sitesDoNotOverlap(sites), "no two sites overlap");
    check(shapesFit(sites), "a retargeted call is a 5 byte call, a hook covers 5 bytes, a block has room for its tail");
    check(clearOfEarlierMilestones(sites), "no site overlaps a milestone 3, 4, 5, 5b, 5d, 5g, 5h, 5i or 8 site");
    check(clearOfJuiced(sites), "no site within 16 bytes of an address Juiced touches");

    const Site* block = siteNamed(sites, "AV1");
    const auto written = partner_replacement::replacementFor(*block, 0x10000000);
    check(written.size() == sizeOf(*block) && written[0] == 0xE8 && written[5] == 0x84 && written[6] == 0xC0 &&
              written[7] == 0x0F && written[8] == 0x85 && written[9] == 0x32,
          "AV1: call, test al,al, jne to 0x64EDA9 (rel 0x32 from 0x64ED77), nops over the rest of its 17 bytes");
    const Site* carry = siteNamed(sites, "AC46");
    check(carry && carry->kind == Kind::CallBlock && sizeOf(*carry) == 22 && partner_sites::parseHex(carry->tail).size() == 17,
          "AC46 (milestone 16 wave 2, DVP9a): the carried count function's first 22 bytes become a call, jz, ret 4 and nops");
    const uintptr_t jumpEnd = block->address + 7 + 6;
    check(jumpEnd + written[9] == 0x64EDA9, "AV1: the jne lands on the original's failure branch");
    return g_failures == 0 ? 0 : 1;
}
