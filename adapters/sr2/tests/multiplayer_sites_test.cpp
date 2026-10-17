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

// Where the `call rel32` of a retargeted call site goes.
uintptr_t callTarget(const Site& site) {
    const auto bytes = partner_sites::parseHex(site.expected);
    const uint32_t displacement = bytes[1] | bytes[2] << 8 | bytes[3] << 16 | static_cast<uint32_t>(bytes[4]) << 24;
    return site.address + kBranchBytes + displacement;
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
        if (site.kind == Kind::PointerSwap && bytes.size() != sizeof(uint32_t)) return false;
    }
    return true;
}

// A bytes site that changes nothing but the jne (75) at `at` into jl (7C).
bool flipsOnlyTheBranchAt(const Site* site, size_t at) {
    if (!site || site->kind != Kind::Bytes) return false;
    auto expected = partner_sites::parseHex(site->expected);
    const auto written = partner_replacement::replacementFor(*site, 0);
    if (expected.size() != written.size() || expected[at] != 0x75 || written[at] != 0x7C) return false;
    expected[at] = written[at];
    return expected == written;
}

// A bytes site whose last byte is the one that changes.
bool flipsOnlyTheBranch(const Site* site) {
    return site && flipsOnlyTheBranchAt(site, partner_sites::parseHex(site->expected).size() - 1);
}

}  // namespace

int main() {

    const std::vector<Site> sites = allSites();
    check(phaseSize("sender scope") == 2, "sender scope: 2 sites (SS1, SS2)");
    check(phaseSize("looks") == 2, "looks: 2 sites (LK1, LK2)");
    check(phaseSize("player traces") == 3, "player traces: 3 sites (TF1, TF2, TL2), written with trace=1 only");
    check(phaseSize("pause flag") == 2, "pause flag: 2 sites (N4a, N4b)");
    check(phaseSize("net clock") == 2, "net clock: 2 sites (CK1, CK2)");
    check(phaseSize("bundle sender trace") == 1, "bundle sender trace: 1 site (BUT), written with trace=1 only");
    check(phaseSize("cutscene handshake") == 2, "cutscene handshake: 2 sites (MC1, MC2)");
    check(phaseSize("guest leave") == 1,
 "guest leave: 1 site (N2a)");
    check(phaseSize("join admission") == 3,
 "join admission: 3 sites (N1a to N1c)");
    check(phaseSize("population budgets") == 3,
 "population budgets: 3 sites (W1a, W1b, W1c)");
    check(phaseSize("population cluster") == 1, "population cluster: 1 site (W2a)");
    check(phaseSize("creation queue") == 4, "creation queue: 4 sites (W8a to W8d)");
    check(phaseSize("upload share") == 1, "upload share: 1 site (W9a)");
    check(phaseSize("mission players") == 3, "mission players: 3 sites (MC3, MC4, MC5), probe gated");
    check(siteNamed(sites, "MC3")->gate == partner_sites::Gate::ProbeT && siteNamed(sites, "MC5")->gate == partner_sites::Gate::ProbeT &&
              siteNamed(sites, "MC4")->gate == partner_sites::Gate::ProbeCp,
          "MC3 and MC5 wait for probe T, MC4 for probe CP");
    check(phaseSize("remote views") == 8, "remote views: 8 sites (W3a to W3h)");
    bool testsCallTheFrustumTest = true;
    for (const char* id : {"W3d", "W3e", "W3f", "W3g", "W3h"}) {
        testsCallTheFrustumTest = testsCallTheFrustumTest && callTarget(*siteNamed(sites, id)) == 0xA020C0;
    }
    check(callTarget(*siteNamed(sites, "MC3")) == 0x814740 && callTarget(*siteNamed(sites, "MC4")) == 0x9D7BD0,
          "MC3 replaces the broadcast 0x814740, MC4 the partner getter 0x9D7BD0");
    check(testsCallTheFrustumTest, "W3d to W3h: each call goes to the frustum test 0xA020C0");
    check(flipsOnlyTheBranchAt(siteNamed(sites, "W3a"), 7) && flipsOnlyTheBranchAt(siteNamed(sites, "W3c"), 7),
          "W3a and W3c: the render frame's jne after the human count compare becomes jl");
    check(phaseSize("population partner") == 16, "population partner: 16 sites (14 calls W10_*, W10a, W10b)");
    bool allCallTheGetter = true;
    for (const Site& site : sites) {
        if (std::string_view(site.id).starts_with("W10_")) allCallTheGetter = allCallTheGetter && callTarget(site) == 0x9D7BD0;
    }
    check(allCallTheGetter, "the 14 W10_* calls all go to get_remote_player (0x9D7BD0)");
    check(flipsOnlyTheBranchAt(siteNamed(sites, "W10a"), 7), "W10a: the same bytes with the jne (75) after the compare turned into jl (7C)");
    const Site* upload = siteNamed(sites, "W9a");
    check(upload && partner_replacement::replacementFor(*upload, 0) == std::vector<uint8_t>(4, 0x90) &&
              partner_sites::parseHex(upload->expected) == std::vector<uint8_t>{0xF7, 0x74, 0x24, 0x18},
          "W9a: the div by the number of remotes (div dword [esp+0x18]) becomes four nops");

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
    const Site* playback = siteNamed(sites, "TF2");
    check(playback && partner_replacement::replacementFor(*playback, 0x10004000) == std::vector<uint8_t>{0x00, 0x40, 0x00, 0x10} &&
              partner_sites::parseHex(playback->expected) == std::vector<uint8_t>{0xD0, 0x33, 0x86, 0x00},
          "TF2: the player bundle's vtable slot (0x8633D0) becomes the thunk, its address little endian");
    const Site* clockMessage = siteNamed(sites, "CK1");
    const Site* clockTick = siteNamed(sites, "CK2");
    check(clockMessage && clockMessage->kind == Kind::HookEntry && clockMessage->address == 0x7FCBF0 && sizeOf(*clockMessage) == 8 &&
              clockTick && clockTick->kind == Kind::HookEntry && clockTick->address == 0x7FC930 && sizeOf(*clockTick) == 10,
          "CK1 and CK2 hook the clock message handler and the net tick, 8 and 10 checked bytes");
    const Site* bundle = siteNamed(sites, "BUT");
    check(bundle && partner_replacement::replacementFor(*bundle, 0x10004000) == std::vector<uint8_t>{0x00, 0x40, 0x00, 0x10} &&
              partner_sites::parseHex(bundle->expected) == std::vector<uint8_t>{0xD0, 0x69, 0x86, 0x00},
          "BUT: the player bundle's unpack slot (0x8669D0) becomes the thunk, its address little endian");
    check(flipsOnlyTheBranch(siteNamed(sites, "W1b")) && flipsOnlyTheBranch(siteNamed(sites, "W1c")),
          "W1b and W1c: the same bytes with the last one, jne (75), turned into jl (7C)");
    return g_failures == 0 ? 0 : 1;
}
