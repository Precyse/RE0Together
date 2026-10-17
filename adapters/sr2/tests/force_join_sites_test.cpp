// The milestone 6 sites as data (no game): the checked bytes, and clearance from every other patch site and from
// everything Juiced touches.
#include <algorithm>
#include <cstdio>
#include <set>
#include <string_view>

#include "../src/force_join_sites.h"
#include "../src/partner_sites.h"
#include "juiced_targets.h"
#include "other_sites.h"

namespace {

using force_join_sites::Site;

constexpr size_t kExpectedSites = 6;
constexpr size_t kMinHookBytes = 5;
constexpr uintptr_t kJuicedMargin = 16;

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_failures;
}

size_t sizeOf(const Site& site) { return partner_sites::parseHex(site.expected).size(); }

bool uniqueAndDisjoint() {
    std::set<std::string_view> ids;
    for (const Site& a : force_join_sites::kSites) {
        if (!ids.insert(a.id).second) return false;
        for (const Site& b : force_join_sites::kSites) {
            if (&a != &b && other_sites::overlaps(a.address, sizeOf(a), b.address, sizeOf(b))) return false;
        }
    }
    return true;
}

bool clearOfJuiced() {
    for (const Site& site : force_join_sites::kSites) {
        const uintptr_t first = site.address - kJuicedMargin;
        const uintptr_t last = site.address + sizeOf(site) + kJuicedMargin;
        if (std::any_of(std::begin(juiced::kTargets), std::end(juiced::kTargets),
                        [&](uint32_t target) { return target >= first && target < last; })) {
            std::printf("     %s at 0x%X is within %u bytes of a Juiced target\n", site.id,
                        static_cast<unsigned>(site.address), static_cast<unsigned>(kJuicedMargin));
            return false;
        }
    }
    return true;
}

bool clearOfOtherMilestones() {
    const auto others = other_sites::withoutForceJoin();
    const auto activities = other_sites::activities();
    for (const Site& site : force_join_sites::kSites) {
        if (other_sites::anyOverlap(others, site.address, sizeOf(site)) ||
            other_sites::anyOverlap(activities, site.address, sizeOf(site))) {
            return false;
        }
    }
    return true;
}

bool functionStartsAt(uintptr_t address) {
    return std::any_of(std::begin(force_join_sites::kSites), std::end(force_join_sites::kSites),
                       [&](const Site& site) { return site.address == address; });
}

}  // namespace

int main() {
    check(force_join_sites::kSiteCount == kExpectedSites, "6 sites: the tick hook, the four game calls and the login");
    check(uniqueAndDisjoint(), "ids are unique and no two sites overlap");
    check(std::all_of(std::begin(force_join_sites::kSites), std::end(force_join_sites::kSites),
                      [](const Site& site) { return sizeOf(site) >= kMinHookBytes; }),
          "every site has the 5 bytes MinHook overwrites");
    check(functionStartsAt(force_join_sites::kFrontEndTick) && functionStartsAt(force_join_sites::kStartMatchmaking) &&
              functionStartsAt(force_join_sites::kStopMatchmaking) && functionStartsAt(force_join_sites::kJoinByIndex) &&
              functionStartsAt(force_join_sites::kServerString) && functionStartsAt(force_join_sites::kLogin),
          "every function address named in the header has a checked row");
    check(partner_sites::parseHex(force_join_sites::kSites[0].expected) ==
              std::vector<uint8_t>{0x81, 0xEC, 0xD0, 0x01, 0x00, 0x00, 0xA1, 0x50, 0x0B, 0xE8, 0x00},
          "F1: sub esp, 0x1D0 and the load of the security cookie");
    check(clearOfJuiced(), "no site within 16 bytes of an address Juiced touches");
    check(clearOfOtherMilestones(), "no site overlaps a milestone 3, 4, 5, 5b, 7 or 8 site");
    return g_failures == 0 ? 0 : 1;
}
