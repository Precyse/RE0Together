// The milestone 5b trace sites as data (no game): the checked bytes, the drop branch decoding and replacement, and
// clearance from every other patch site and from everything Juiced touches.
#include <algorithm>
#include <cstdio>
#include <set>
#include <string_view>

#include "../src/join_net_trace_sites.h"
#include "../src/partner_sites.h"
#include "../src/player_cap_patches.h"
#include "../src/replication_sites.h"
#include "juiced_targets.h"

namespace {

using join_net_trace_sites::Kind;
using join_net_trace_sites::Site;

constexpr size_t kExpectedSites = 11;
constexpr size_t kExpectedEntries = 9;
constexpr size_t kMinEntryBytes = 5;
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
    for (const Site& a : join_net_trace_sites::kSites) {
        if (!ids.insert(a.id).second) return false;
        for (const Site& b : join_net_trace_sites::kSites) {
            if (&a != &b && overlaps(a.address, sizeOf(a), b.address, sizeOf(b))) return false;
        }
    }
    return true;
}

bool shapesFit() {
    for (const Site& site : join_net_trace_sites::kSites) {
        const auto bytes = partner_sites::parseHex(site.expected);
        if (site.kind == Kind::Entry && bytes.size() < kMinEntryBytes) return false;
        if (site.kind == Kind::DropBranch &&
            (bytes.size() != join_net_trace_sites::kBranchBytes || bytes[0] != 0x0F || bytes[1] != 0x85)) {
            return false;
        }
    }
    return true;
}

const Site* siteNamed(std::string_view id) {
    for (const Site& site : join_net_trace_sites::kSites) {
        if (id == site.id) return &site;
    }
    return nullptr;
}

bool clearOfJuiced() {
    for (const Site& site : join_net_trace_sites::kSites) {
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
    const auto playerCap = player_cap_patches::forPlayers(kMaxPlayers, {kThunk, kThunk});
    for (const Site& site : join_net_trace_sites::kSites) {
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
    }
    return true;
}

}  // namespace

int main() {
    check(join_net_trace_sites::kSiteCount == kExpectedSites,
          "11 sites: T1, T2, T4 to T10 and the two drop branches of T4 (T3 moved to the sender scope of milestone 9)");
    check(std::count_if(std::begin(join_net_trace_sites::kSites), std::end(join_net_trace_sites::kSites),
                        [](const Site& s) { return s.kind == Kind::Entry; }) == kExpectedEntries,
          "9 of them are function entries");
    check(uniqueAndDisjoint(), "ids are unique and no two sites overlap");
    check(shapesFit(), "an entry has the 5 bytes MinHook overwrites, a drop branch is a 6 byte jne rel32");
    check(join_net_trace_sites::takenTarget(*siteNamed("T4a")) == join_net_trace_sites::kJoinReqDropExit &&
              join_net_trace_sites::takenTarget(*siteNamed("T4b")) == join_net_trace_sites::kJoinReqDropExit,
          "both drop branches go to 0x81611E");
    check(siteNamed("T4a")->address == join_net_trace_sites::kJoinReqMappedDrop &&
              siteNamed("T4b")->address == join_net_trace_sites::kJoinReqDuplicateDrop &&
              siteNamed("T4")->address == join_net_trace_sites::kJoinReqHandler,
          "the drop branches are the two of the join_req handler 0x815BC0");

    const CodePatch entry = join_net_trace_sites::patchFor(*siteNamed("T6"), kThunk);
    check(entry.address == 0x80F640 && entry.expected.size() == 8 && entry.replacement.empty(),
          "an entry is only checked, MinHook writes it");
    const CodePatch drop = join_net_trace_sites::patchFor(*siteNamed("T4b"), kThunk);
    const uint32_t displacement = static_cast<uint32_t>(kThunk - (0x815EB5 + 5));
    check(drop.expected == std::vector<uint8_t>{0x0F, 0x85, 0x63, 0x02, 0x00, 0x00} &&
              drop.replacement == std::vector<uint8_t>{0xE9, static_cast<uint8_t>(displacement),
                                                       static_cast<uint8_t>(displacement >> 8),
                                                       static_cast<uint8_t>(displacement >> 16),
                                                       static_cast<uint8_t>(displacement >> 24), 0x90},
          "a drop branch becomes jmp rel32 to its thunk and a nop; the original 6 bytes are checked");
    check(clearOfJuiced(), "no site within 16 bytes of an address Juiced touches");
    check(clearOfOtherMilestones(), "no site overlaps a milestone 3, 4 or 5 site");
    return g_failures == 0 ? 0 : 1;
}
