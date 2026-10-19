// The milestone 16 W2 site tables (no game): 16 rows in two sets, their shapes, call targets and tails, and clearance from every other
// milestone's site and from everything Juiced touches.
#include <algorithm>
#include <cstdio>
#include <set>
#include <span>
#include <string_view>
#include <vector>

#include "../src/revive_sites.h"
#include "juiced_targets.h"
#include "other_sites.h"

namespace {

using partner_sites::Kind;
using partner_sites::Site;

constexpr uintptr_t kClearance = 16;
constexpr size_t kBranchBytes = 5;
constexpr size_t kRevivedRows = 12;
constexpr size_t kNotorietyRows = 4;
constexpr uintptr_t kPartnerGetter = 0x9D7BD0;
constexpr uintptr_t kWaitingQuery = 0x8CBDB0;
constexpr uintptr_t kTeamSees = 0x55DEC0;

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_failures;
}

std::vector<Site> setOf(std::span<const Site> sites) { return {sites.begin(), sites.end()}; }

std::vector<Site> allSites() {
    std::vector<Site> all = setOf(revive_sites::kRevive);
    const std::vector<Site> notoriety = setOf(revive_sites::kNotoriety);
    all.insert(all.end(), notoriety.begin(), notoriety.end());
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

// The ids of every site table of the other milestones.
std::set<std::string_view> otherIds() {
    std::set<std::string_view> ids;
    for (const Site& site : partner_sites::kSites) ids.insert(site.id);
    for (const multiplayer_sites::Phase& phase : multiplayer_sites::kPhases) {
        for (const Site& site : phase.sites) ids.insert(site.id);
    }
    for (const auto table : {std::span<const Site>(activity_sites::kPhase1), std::span<const Site>(activity_sites::kPhase2),
                             std::span<const Site>(activity_sites::kPhase3)}) {
        for (const Site& site : table) ids.insert(site.id);
    }
    return ids;
}

bool idsAreUnique(const std::vector<Site>& sites) {
    const std::set<std::string_view> others = otherIds();
    std::set<std::string_view> ids;
    std::set<uintptr_t> addresses;
    for (const Site& site : sites) {
        if (!ids.insert(site.id).second || !addresses.insert(site.address).second || others.count(site.id) != 0) return false;
    }
    return true;
}

bool retargetsStartWithCall(const std::vector<Site>& sites) {
    for (const Site& site : sites) {
        if (site.kind != Kind::RetargetCall) continue;
        const auto bytes = partner_sites::parseHex(site.expected);
        if (bytes.size() != kBranchBytes || bytes[0] != 0xE8) return false;
    }
    return true;
}

bool checkRowsHaveNoCode(const std::vector<Site>& sites) {
    for (const char* id : {"RW2a", "RW2b", "RM1r"}) {
        const Site* site = siteNamed(sites, id);
        if (!site || site->kind != Kind::Bytes || !site->tail.empty() || site->thunk != partner_sites::Thunk::None) return false;
    }
    return true;
}

bool overlapsBetweenSets() {
    for (const Site& a : revive_sites::kRevive) {
        for (const Site& b : revive_sites::kNotoriety) {
            if (other_sites::overlaps(a.address, sizeOf(a), b.address, sizeOf(b))) return true;
        }
    }
    return false;
}

bool clearOfOtherSites(const std::vector<Site>& sites) {
    std::vector<other_sites::Range> others = other_sites::milestones3to8();
    for (const auto& more : {other_sites::activities(), other_sites::players3and4()}) others.insert(others.end(), more.begin(), more.end());
    for (const Site& site : sites) {
        if (other_sites::anyOverlap(others, site.address - kClearance, sizeOf(site) + 2 * kClearance)) {
            std::printf("     %s at 0x%X is within %u bytes of another site\n", site.id, static_cast<unsigned>(site.address),
                        static_cast<unsigned>(kClearance));
            return false;
        }
    }
    return true;
}

bool clearOfJuiced(const std::vector<Site>& sites) {
    for (const Site& site : sites) {
        const uintptr_t first = site.address - kClearance;
        const uintptr_t last = site.address + sizeOf(site) + kClearance;
        if (std::any_of(std::begin(juiced::kTargets), std::end(juiced::kTargets),
                        [&](uint32_t target) { return target >= first && target < last; })) {
            std::printf("     %s at 0x%X is within %u bytes of a Juiced target\n", site.id, static_cast<unsigned>(site.address),
                        static_cast<unsigned>(kClearance));
            return false;
        }
    }
    return true;
}

}  // namespace

int main() {
    const std::vector<Site> sites = allSites();
    check(std::size(revive_sites::kRevive) == kRevivedRows && std::size(revive_sites::kNotoriety) == kNotorietyRows && sites.size() == 16,
          "16 rows: 12 downed and revive, 4 notoriety");
    check(idsAreUnique(sites), "ids and addresses are unique, and no id is used by another milestone");
    check(retargetsStartWithCall(sites), "every call retarget is a 5 byte call (E8)");
    check(checkRowsHaveNoCode(sites), "RW2a, RW2b and RM1r are checks: a Bytes row with an empty tail and no thunk");

    const Site* rw4 = siteNamed(sites, "RW4");
    check(rw4 && rw4->kind == Kind::HookEntry && rw4->address == 0x8CBD90 && sizeOf(*rw4) == 8, "RW4 hooks the reset 0x8CBD90, 8 checked bytes");
    const Site* rw2 = siteNamed(sites, "RW2");
    const Site* rm1 = siteNamed(sites, "RM1");
    check(rw2 && rm1 && rw2->kind == Kind::JumpOut && rm1->kind == Kind::JumpOut && sizeOf(*rw2) == kBranchBytes && sizeOf(*rm1) == kBranchBytes,
          "RW2 and RM1 are 5 byte jump outs");
    check(callTarget(*siteNamed(sites, "RW1")) == kWaitingQuery && callTarget(*siteNamed(sites, "RW3")) == kWaitingQuery,
          "RW1 and RW3 each call the waiting query 0x8CBDB0");
    check(callTarget(*siteNamed(sites, "RA1")) == kPartnerGetter && callTarget(*siteNamed(sites, "RA2")) == kPartnerGetter &&
              callTarget(*siteNamed(sites, "RA3")) == kPartnerGetter && callTarget(*siteNamed(sites, "RK1")) == kPartnerGetter &&
              callTarget(*siteNamed(sites, "NV1")) == kPartnerGetter,
          "RA1 to RA3, RK1 and NV1 each call the partner getter 0x9D7BD0");
    check(callTarget(*siteNamed(sites, "NW1")) == kTeamSees, "NW1 calls the team sees test 0x55DEC0");
    check(siteNamed(sites, "RA1")->thunk == siteNamed(sites, "RA2")->thunk && siteNamed(sites, "RA1")->thunk != siteNamed(sites, "RA3")->thunk,
          "RA1 and RA2 share the anchor thunk, RA3 has the one that logs");
    check(siteNamed(sites, "RK1")->thunk == partner_sites::Thunk::MatchRemoteEsi, "RK1 reuses the existing MatchRemoteEsi call replacement");

    for (const char* id : {"NL1", "NL2"}) {
        const Site* site = siteNamed(sites, id);
        check(site && site->kind == Kind::CallBlock && sizeOf(*site) == 7 && site->tail == "90 90", "NL1 and NL2 are 7 byte call blocks with a two nop tail");
    }

    check(!overlapsBetweenSets(), "the two sets do not overlap");
    check(clearOfOtherSites(sites), "every site is 16 bytes clear of the sites of the other milestones");
    check(clearOfJuiced(sites), "every site is 16 bytes clear of an address Juiced touches");
    return g_failures == 0 ? 0 : 1;
}
