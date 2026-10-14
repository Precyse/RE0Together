// The milestone 5 code edits as data (no game): the written bytes, the shipped co-op rule, and clearance from the
// other patch sites and from everything Juiced touches.
#include <algorithm>
#include <cstdio>

#include "../src/partner_sites.h"
#include "../src/player_cap_patches.h"
#include "../src/replication_sites.h"
#include "juiced_targets.h"

namespace {

constexpr uintptr_t kThunk = 0x10002000;
constexpr uintptr_t kJuicedMargin = 16;
constexpr uint32_t kFourPlayers = 4;
constexpr uint32_t kEightPlayers = 8;

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_failures;
}

bool overlaps(const CodePatch& patch, uintptr_t address, size_t size) {
    return patch.address < address + size && address < patch.address + patch.expected.size();
}

bool clearOfJuiced(const std::vector<CodePatch>& patches) {
    for (const CodePatch& patch : patches) {
        const uintptr_t first = patch.address - kJuicedMargin;
        const uintptr_t last = patch.address + patch.expected.size() + kJuicedMargin;
        if (std::any_of(std::begin(juiced::kTargets), std::end(juiced::kTargets),
                        [&](uint32_t target) { return target >= first && target < last; })) {
            return false;
        }
    }
    return true;
}

bool clearOfEarlierMilestones(const std::vector<CodePatch>& patches) {
    for (const CodePatch& patch : patches) {
        for (const partner_sites::Site& site : partner_sites::kSites) {
            if (overlaps(patch, site.address, partner_sites::parseHex(site.expected).size())) return false;
        }
        for (const auto& entry : replication_sites::kEntries) {
            if (overlaps(patch, entry.n1, replication_sites::kEntryProbeBytes)) return false;
        }
        for (const auto& branch : replication_sites::kBranches) {
            if (overlaps(patch, branch.address, 2)) return false;
        }
    }
    return true;
}

}  // namespace

int main() {
    check(player_cap_patches::sessionInfoMask(kFourPlayers) == 0xF8, "4 players: mask F8");
    check(player_cap_patches::sessionInfoMask(3) == 0xF7 && player_cap_patches::sessionInfoMask(kEightPlayers) == 0xFC,
          "3 players F7, 8 players FC");

    const auto patches = player_cap_patches::forPlayers(kFourPlayers, kThunk);
    check(patches.size() == 2, "two edits");
    const CodePatch& max = patches[0];
    check(max.address == 0x82FD81 && max.expected == std::vector<uint8_t>{0x83, 0xE3, 0xF6, 0x83, 0xC3, 0x0C},
          "session info maximum: 83 E3 F6 83 C3 0C at 0x82FD81 is checked");
    check(max.replacement == std::vector<uint8_t>{0x83, 0xE3, 0xF8, 0x83, 0xC3, 0x0C},
          "session info maximum: only the byte at 0x82FD83 changes (F6 to F8)");
    const CodePatch& test = patches[1];
    check(test.address == 0x7F7AFD && test.expected == std::vector<uint8_t>{0x83, 0x79, 0x30, 0x02, 0x75, 0x2D},
          "isCoop: 83 79 30 02 75 2D at 0x7F7AFD is checked");
    const uint32_t displacement = static_cast<uint32_t>(kThunk - (0x7F7AFD + 5));
    check(test.replacement == std::vector<uint8_t>{0xE9, static_cast<uint8_t>(displacement),
                                                   static_cast<uint8_t>(displacement >> 8),
                                                   static_cast<uint8_t>(displacement >> 16),
                                                   static_cast<uint8_t>(displacement >> 24), 0x90},
          "isCoop: jmp rel32 to the thunk and a nop");

    check(player_cap_patches::forPlayers(kEightPlayers, kThunk)[0].replacement[2] == 0xFC, "8 players write FC");
    check(player_cap_patches::forPlayers(2, kThunk).empty() && player_cap_patches::forPlayers(1, kThunk).empty(),
          "max_players 2 (the shipped co-op) patches nothing");
    check(patches[0].address + patches[0].expected.size() <= patches[1].address ||
              patches[1].address + patches[1].expected.size() <= patches[0].address,
          "the two edits do not overlap");
    check(clearOfJuiced(patches), "no edit within 16 bytes of an address Juiced touches");
    check(clearOfEarlierMilestones(patches), "no edit overlaps a milestone 3 or 4 site");
    return g_failures == 0 ? 0 : 1;
}
