// The milestone 6 sites as data (no game): the checked bytes, and clearance from every other patch site and from
// everything Juiced touches.
#include <algorithm>
#include <cstdio>
#include <set>
#include <string_view>

#include "../src/force_join_sites.h"
#include "../src/join_net_trace_sites.h"
#include "../src/partner_sites.h"
#include "../src/player_cap_patches.h"
#include "../src/replication_sites.h"
#include "juiced_targets.h"

namespace {

using force_join_sites::Site;

constexpr size_t kExpectedSites = 6;
constexpr size_t kMinHookBytes = 5;
constexpr uintptr_t kJuicedMargin = 16;
constexpr uintptr_t kThunk = 0x10003000;
constexpr uint32_t kMaxPlayers = 4;

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_failures;
}

size_t sizeOf(const Site& site) { return partner_sites::parseHex(site.expected).size(); }

bool overlaps(uintptr_t a, size_t aSize, uintptr_t b, size_t bSize) { return a < b + bSize && b < a + aSize; }

bool uniqueAndDisjoint() {
    std::set<std::string_view> ids;
    for (const Site& a : force_join_sites::kSites) {
        if (!ids.insert(a.id).second) return false;
        for (const Site& b : force_join_sites::kSites) {
            if (&a != &b && overlaps(a.address, sizeOf(a), b.address, sizeOf(b))) return false;
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
    const auto playerCap = player_cap_patches::forPlayers(kMaxPlayers, kThunk);
    for (const Site& site : force_join_sites::kSites) {
        const size_t size = sizeOf(site);
        for (const partner_sites::Site& other : partner_sites::kSites) {
            if (overlaps(site.address, size, other.address, partner_sites::parseHex(other.expected).size())) return false;
        }
        for (const auto& entry : replication_sites::kEntries) {
            if (overlaps(site.address, size, entry.n1, replication_sites::kEntryProbeBytes)) return false;
        }
        for (const auto& branch : replication_sites::kBranches) {
            if (overlaps(site.address, size, branch.address, 2)) return false;
        }
        for (const CodePatch& patch : playerCap) {
            if (overlaps(site.address, size, patch.address, patch.expected.size())) return false;
        }
        for (const auto& trace : join_net_trace_sites::kSites) {
            if (overlaps(site.address, size, trace.address, partner_sites::parseHex(trace.expected).size())) return false;
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
    check(clearOfOtherMilestones(), "no site overlaps a milestone 3, 4, 5 or 5b site");
    return g_failures == 0 ? 0 : 1;
}
