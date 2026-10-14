// Milestone 5e (no game): the port rules, the code edits as data, and clearance from the other patch sites and from
// everything Juiced touches.
#include <algorithm>
#include <cstdio>

#include "../src/game_port_patches.h"
#include "../src/game_port_rules.h"
#include "../src/join_net_trace_sites.h"
#include "../src/partner_sites.h"
#include "../src/player_cap_patches.h"
#include "../src/replication_sites.h"
#include "juiced_targets.h"

namespace {

constexpr uintptr_t kThunk = 0x10004000;
constexpr uintptr_t kOtherThunk = 0x10002000;
constexpr uintptr_t kJuicedMargin = 16;
constexpr uint32_t kFourPlayers = 4;
constexpr uint32_t kMovedPort = 4201;
constexpr size_t kAllSites = 8;  // E1, six E2, E3

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_failures;
}

bool overlaps(uintptr_t a, size_t aSize, uintptr_t b, size_t bSize) { return a < b + bSize && b < a + aSize; }

bool clearOfJuiced(const std::vector<CodePatch>& patches) {
    for (const CodePatch& patch : patches) {
        const uintptr_t first = patch.address - kJuicedMargin;
        const uintptr_t last = patch.address + patch.expected.size() + kJuicedMargin;
        if (std::any_of(std::begin(juiced::kTargets), std::end(juiced::kTargets),
                        [&](uint32_t target) { return target >= first && target < last; })) {
            std::printf("     %s at 0x%X is within %u bytes of a Juiced target\n", patch.name,
                        static_cast<unsigned>(patch.address), static_cast<unsigned>(kJuicedMargin));
            return false;
        }
    }
    return true;
}

bool clearOfOtherMilestones(const std::vector<CodePatch>& patches) {
    const player_cap_patches::Thunks thunks{kOtherThunk, kOtherThunk, kOtherThunk};
    const auto playerCap = player_cap_patches::forPlayers(kFourPlayers, thunks);
    for (const CodePatch& patch : patches) {
        const size_t size = patch.expected.size();
        for (const partner_sites::Site& site : partner_sites::kSites) {
            if (overlaps(patch.address, size, site.address, partner_sites::parseHex(site.expected).size())) return false;
        }
        for (const auto& entry : replication_sites::kEntries) {
            if (overlaps(patch.address, size, entry.n1, replication_sites::kEntryProbeBytes)) return false;
        }
        for (const auto& branch : replication_sites::kBranches) {
            if (overlaps(patch.address, size, branch.address, 2)) return false;
        }
        for (const CodePatch& other : playerCap) {
            if (overlaps(patch.address, size, other.address, other.expected.size())) return false;
        }
        for (const auto& trace : join_net_trace_sites::kSites) {
            if (overlaps(patch.address, size, trace.address, partner_sites::parseHex(trace.expected).size())) return false;
        }
    }
    return true;
}

bool disjoint(const std::vector<CodePatch>& patches) {
    for (const CodePatch& a : patches) {
        for (const CodePatch& b : patches) {
            if (&a != &b && overlaps(a.address, a.expected.size(), b.address, b.expected.size())) return false;
        }
    }
    return true;
}

const CodePatch* patchAt(const std::vector<CodePatch>& patches, uintptr_t address) {
    for (const CodePatch& patch : patches) {
        if (patch.address == address) return &patch;
    }
    return nullptr;
}

}  // namespace

int main() {
    using namespace game_port_rules;
    check(valid(1024) && valid(4200) && valid(65535), "1024, 4200 and 65535 are valid ports");
    check(!valid(0) && !valid(1023) && !valid(65536) && !valid(4294967295UL), "0, 1023, 65536 and larger are not");
    check(peerPort(true, 50001) == 4200, "the host is reached at 4200, whatever was relayed");
    check(peerPort(false, 4201) == 4201 && peerPort(false, 50001) == 50001, "another guest is reached at the relayed port");
    check(peerPort(false, 0) == 4200, "without a relayed port the shipped one stays");

    const auto moved = game_port_patches::forPort(kMovedPort, kFourPlayers, kThunk);
    check(moved.size() == kAllSites, "4 players and a moved port: eight sites");
    const CodePatch* bind = patchAt(moved, 0xC1BBAF);
    check(bind && bind->expected == std::vector<uint8_t>{0xB8, 0x68, 0x10, 0x00, 0x00} &&
              bind->replacement == std::vector<uint8_t>{0xB8, 0x69, 0x10, 0x00, 0x00},
          "E1: B8 68 10 00 00 is checked, the immediate becomes 4201");
    const CodePatch* first = patchAt(moved, 0x811AC0);
    check(first && first->expected == std::vector<uint8_t>{0x66, 0xC7, 0x40, 0x04, 0x68, 0x10} &&
              first->replacement == std::vector<uint8_t>{0x66, 0xC7, 0x40, 0x04, 0x69, 0x10},
          "E2: only the port bytes change, the register form stays");
    check(patchAt(moved, 0x811CB7) && patchAt(moved, 0x811CB7)->replacement[2] == 0x41, "E2: the last site keeps its [ecx+4] form");
    const CodePatch* quick = patchAt(moved, 0x8CDA3B);
    const uint32_t displacement = static_cast<uint32_t>(kThunk - (0x8CDA3B + 5));
    check(quick && quick->expected == std::vector<uint8_t>{0x66, 0xC7, 0x46, 0x2C, 0x68, 0x10} &&
              quick->replacement == std::vector<uint8_t>{0xE8, static_cast<uint8_t>(displacement),
                                                         static_cast<uint8_t>(displacement >> 8),
                                                         static_cast<uint8_t>(displacement >> 16),
                                                         static_cast<uint8_t>(displacement >> 24), 0x90},
          "E3: the mov becomes call rel32 to the thunk and a nop");
    check(std::all_of(moved.begin(), moved.end(), [](const CodePatch& p) { return p.replacement.size() <= p.expected.size(); }),
          "no replacement is longer than the bytes checked");

    const auto shipped = game_port_patches::forPort(4200, kFourPlayers, kThunk);
    check(shipped.size() == kAllSites && patchAt(shipped, 0xC1BBAF)->replacement.empty() &&
              patchAt(shipped, 0x811B11)->replacement.empty() && !patchAt(shipped, 0x8CDA3B)->replacement.empty(),
          "port 4200 with 4 players: every site checked, only the quick check written");

    const auto twoPlayers = game_port_patches::forPort(kMovedPort, 2, kThunk);
    check(twoPlayers.size() == 1 && twoPlayers[0].address == 0xC1BBAF && !twoPlayers[0].replacement.empty(),
          "2 players and a moved port: only E1 (E2 and E3 follow the max_players gate)");
    check(game_port_patches::forPort(4200, 2, kThunk).empty(), "port 4200 with 2 players: nothing at all");

    check(disjoint(moved), "no two sites overlap");
    check(clearOfJuiced(moved), "no site within 16 bytes of an address Juiced touches");
    check(clearOfOtherMilestones(moved), "no site overlaps a milestone 3, 4, 5, 5b or 5d site");
    return g_failures == 0 ? 0 : 1;
}
