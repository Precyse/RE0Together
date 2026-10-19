// The milestone 16 wave 1 site tables (no game): five rows, their shapes and call targets, the sets, and clearance from every
// other milestone's site and from everything Juiced touches.
#include <algorithm>
#include <cstdio>
#include <set>
#include <string_view>
#include <vector>

#include "../src/session_kick_sites.h"
#include "juiced_targets.h"
#include "other_sites.h"

namespace {

using partner_sites::Kind;
using partner_sites::Site;

constexpr uintptr_t kClearance = 16;
constexpr size_t kBranchBytes = 5;
constexpr size_t kHookBytes = 8;
constexpr uintptr_t kPartnerMemberGetter = 0x8CA380;
constexpr uintptr_t kSessionRemoveMember = 0x80F9B0;

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_failures;
}

std::vector<Site> ourSites() {
    std::vector<Site> all;
    for (const session_kick_sites::Set& set : session_kick_sites::kSets) all.insert(all.end(), set.sites.begin(), set.sites.end());
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

size_t setSize(std::string_view name) {
    for (const session_kick_sites::Set& set : session_kick_sites::kSets) {
        if (name == set.name) return set.sites.size();
    }
    return 0;
}

// The ids of every site table of the other milestones.
std::set<std::string_view> otherIds() {
    std::set<std::string_view> ids;
    for (const Site& site : partner_sites::kSites) ids.insert(site.id);
    for (const multiplayer_sites::Phase& phase : multiplayer_sites::kPhases) {
        for (const Site& site : phase.sites) ids.insert(site.id);
    }
    for (const auto table : multiplayer_sites::kScriptTraces) {
        for (const Site& site : table) ids.insert(site.id);
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

bool clearOfOtherSites(const std::vector<Site>& sites) {
    std::vector<other_sites::Range> others = other_sites::milestones3to8();
    for (const auto& more : {other_sites::activities(), other_sites::players3and4()}) others.insert(others.end(), more.begin(), more.end());
    for (const auto table : multiplayer_sites::kScriptTraces) {
        for (const Site& site : table) others.push_back({site.address, sizeOf(site)});
    }
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
    const std::vector<Site> sites = ourSites();
    check(sites.size() == 5, "5 rows: KA1 to KA5");
    check(setSize("kick target") == 4 && setSize("session end") == 1, "two sets: kick target 4 rows (KA1 to KA4), session end 1 (KA5)");
    check(idsAreUnique(sites), "ids and addresses are unique, and no id is used by another milestone");

    const Site* picker = siteNamed(sites, "KA1");
    check(picker && picker->kind == Kind::HookEntry && picker->address == 0x7EF5E0 && sizeOf(*picker) == kHookBytes &&
              partner_sites::parseHex(picker->expected) == std::vector<uint8_t>{0x81, 0xEC, 0x08, 0x03, 0x00, 0x00, 0xA1, 0x50},
          "KA1 hooks coop_kick_player 0x7EF5E0, 8 checked bytes");

    bool retargets = true;
    for (const char* id : {"KA2", "KA3", "KA4", "KA5"}) {
        const Site* site = siteNamed(sites, id);
        const auto bytes = site ? partner_sites::parseHex(site->expected) : std::vector<uint8_t>();
        retargets = retargets && site && site->kind == Kind::RetargetCall && bytes.size() == kBranchBytes && bytes[0] == 0xE8;
    }
    check(retargets, "KA2 to KA5 are 5 byte call retargets (E8)");

    check(siteNamed(sites, "KA2")->address == 0x7EF5F6 && siteNamed(sites, "KA3")->address == 0x7EF534 &&
              siteNamed(sites, "KA4")->address == 0x7EEB18 && siteNamed(sites, "KA5")->address == 0x69A1D6,
          "KA2 is the confirm builder's getter call, KA3 the confirm callback's, KA4 kick_coop_player's, KA5 the sign-in loss removal");
    check(callTarget(*siteNamed(sites, "KA2")) == kPartnerMemberGetter && callTarget(*siteNamed(sites, "KA3")) == kPartnerMemberGetter &&
              callTarget(*siteNamed(sites, "KA4")) == kPartnerMemberGetter,
          "KA2 to KA4 each call the partner member getter 0x8CA380");
    check(callTarget(*siteNamed(sites, "KA5")) == kSessionRemoveMember, "KA5 calls the member removal 0x80F9B0");
    check(siteNamed(sites, "KA3")->thunk == siteNamed(sites, "KA4")->thunk && siteNamed(sites, "KA2")->thunk != siteNamed(sites, "KA3")->thunk,
          "KA3 and KA4 share the thunk that uses the choice up; KA2 only reads it");

    check(clearOfOtherSites(sites), "every site is 16 bytes clear of the sites of the other milestones");
    check(clearOfJuiced(sites), "every site is 16 bytes clear of an address Juiced touches");
    return g_failures == 0 ? 0 : 1;
}
