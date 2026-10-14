// The milestone 9 site tables (no game): ids and addresses, shapes, the bytes written, clearance from every other site
// and from everything Juiced touches, and which phases wait for trace=1.
#include <algorithm>
#include <cstdio>
#include <set>
#include <string_view>
#include <vector>

#include "../src/multiplayer_sites.h"
#include "../src/partner_replacement.h"
#include "juiced_targets.h"
#include "other_sites.h"

namespace {

using partner_sites::Kind;
using partner_sites::Site;

constexpr uintptr_t kJuicedMargin = 16;
constexpr size_t kBranchBytes = 5;
constexpr uintptr_t kThunk = 0x10004000;

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_failures;
}

std::vector<Site> allSites() {
    std::vector<Site> all;
    for (const multiplayer_sites::Phase& phase : multiplayer_sites::kPhases) all.insert(all.end(), phase.sites.begin(), phase.sites.end());
    return all;
}

size_t sizeOf(const Site& site) { return partner_sites::parseHex(site.expected).size(); }

const Site* siteNamed(const std::vector<Site>& sites, std::string_view id) {
    for (const Site& site : sites) {
        if (id == site.id) return &site;
    }
    return nullptr;
}

size_t phaseSize(std::string_view name) {
    for (const multiplayer_sites::Phase& phase : multiplayer_sites::kPhases) {
        if (name == phase.name) return phase.sites.size();
    }
    return 0;
}

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
            if (&a != &b && other_sites::overlaps(a.address, sizeOf(a), b.address, sizeOf(b))) return false;
        }
    }
    return true;
}

bool clearOfOtherMilestones(const std::vector<Site>& sites) {
    std::vector<other_sites::Range> others = other_sites::milestones3to8();
    const auto activities = other_sites::activities();
    others.insert(others.end(), activities.begin(), activities.end());
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
        if (site.kind == Kind::JumpOut && bytes.size() < kBranchBytes) return false;
    }
    return true;
}

// A bytes site that changes nothing but its last byte from jne to jl.
bool flipsOnlyTheBranch(const Site* site) {
    if (!site || site->kind != Kind::Bytes) return false;
    auto expected = partner_sites::parseHex(site->expected);
    const auto written = partner_replacement::replacementFor(*site, 0);
    if (expected.size() != written.size() || expected.back() != 0x75 || written.back() != 0x7C) return false;
    expected.back() = written.back();
    return expected == written;
}

}  // namespace

int main() {

    const std::vector<Site> sites = allSites();
    check(phaseSize("sender scope") == 2, "sender scope: 2 sites (SS1, SS2)");
    check(phaseSize("looks") == 2, "looks: 2 sites (LK1, LK2)");
    check(phaseSize("pause flag") == 2, "pause flag: 2 sites (N4a, N4b)");
    check(phaseSize("cutscene handshake") == 2, "cutscene handshake: 2 sites (MC1, MC2)");
    check(phaseSize("guest leave") == 1,
 "guest leave: 1 site (N2a)");
    check(phaseSize("join admission") == 3,
 "join admission: 3 sites (N1a to N1c)");
    check(phaseSize("population budgets") == 3,
 "population budgets: 3 sites (W1a, W1b, W1c)");
    check(phaseSize("population cluster") == 1, "population cluster: 1 site (W2a)");
    check(phaseSize("creation queue") == 4, "creation queue: 4 sites (W8a to W8d)");

    check(idsAndAddressesAreUnique(sites), "ids and addresses are unique");
    check(sitesDoNotOverlap(sites), "no two sites overlap");
    check(shapesFit(sites), "a retargeted call is a 5 byte call, a hook or a jump covers 5 bytes, a block has room for its tail");
    check(clearOfOtherMilestones(sites), "no site overlaps a site of milestones 3 to 8");
    check(clearOfJuiced(sites), "no site within 16 bytes of an address Juiced touches");

    const Site* dispatch = siteNamed(sites, "SS1");
    const Site* inOrder = siteNamed(sites, "SS2");
    check(dispatch && dispatch->address == 0x8138F0 && partner_sites::parseHex(dispatch->expected).size() == 8 &&
              inOrder && inOrder->address == 0x813390 && partner_sites::parseHex(inOrder->expected).size() == 8,
          "SS1 and SS2 hook the two dispatchers, 8 checked bytes each");
    check(dispatch && partner_replacement::replacementFor(*dispatch, kThunk).empty(), "a hook is written by MinHook, not by the table");
    check(flipsOnlyTheBranch(siteNamed(sites, "W1b")) && flipsOnlyTheBranch(siteNamed(sites, "W1c")),
          "W1b and W1c: the same bytes with the last one, jne (75), turned into jl (7C)");
    return g_failures == 0 ? 0 : 1;
}
