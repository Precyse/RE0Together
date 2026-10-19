// The remote player visuals of milestone 14 (no game): the skin composite pool immediates for any number of players against the
// shipped bytes, the sites as data, the hide and kill rules of the cutscene start, and clearance from every other site and Juiced.
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../src/player_sites.h"
#include "../src/player_visuals_rules.h"
#include "juiced_targets.h"
#include "other_sites.h"

namespace {

using player_sites::kSkinPool;

constexpr uintptr_t kJuicedMargin = 16;
constexpr uint32_t kShippedBase = 0x21F2988;

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_failures;
}

uint32_t immediateOf(const player_sites::SkinPoolSite& site) {
    const auto bytes = partner_sites::parseHex(site.expected);
    uint32_t value = 0;
    std::memcpy(&value, bytes.data() + site.immediateOffset, sizeof(value));
    return value;
}

void theSkinPoolImmediates() {
    const auto shipped = player_visuals_rules::skinPoolImmediates(kShippedBase, player_visuals_rules::kShippedSkinObjects);
    bool same = std::size(kSkinPool) == shipped.size();
    for (size_t i = 0; i < std::size(kSkinPool) && same; ++i) same = immediateOf(kSkinPool[i]) == shipped[i];
    check(same, "for the shipped base and 2 objects the rule gives the eight dwords the exe has (PV3 to PV10)");
    check(shipped[1] == 0x21F2A78 && shipped[3] == 0x21F2A9C && shipped[7] == 0x21F2AC4, "the end of 2 objects is 0x21F2A78, +0x24 0x21F2A9C, +0x4C 0x21F2AC4");

    constexpr uint32_t kNewBase = 0x30000000;
    const auto four = player_visuals_rules::skinPoolImmediates(kNewBase, 4);
    check(four[0] == kNewBase && four[1] == kNewBase + 4 * 0x78 && four[2] == kNewBase + 0x24 && four[3] == kNewBase + 4 * 0x78 + 0x24 &&
              four[4] == four[2] && four[5] == four[3] && four[6] == kNewBase + 0x4C && four[7] == kNewBase + 4 * 0x78 + 0x4C,
          "for 4 players on a new block: base, end = base + 4 x 0x78, and both moved by 0x24 and by 0x4C");
    check(player_visuals_rules::skinPoolImmediates(kNewBase, 8)[1] - kNewBase == 8 * 0x78, "8 players: the end is 8 x 0x78 on");
}

void theSkinPoolSites() {
    check(std::size(kSkinPool) == 8, "eight immediates, PV3 to PV10");
    bool shapes = true;
    for (const auto& site : kSkinPool) {
        const auto bytes = partner_sites::parseHex(site.expected);
        const bool movImmediate = bytes.size() == 5 && site.immediateOffset == 1 && (bytes[0] == 0xBE || bytes[0] == 0xBB);
        const bool compareImmediate = bytes.size() == 6 && site.immediateOffset == 2 && bytes[0] == 0x81 && (bytes[1] == 0xFE || bytes[1] == 0xFB);
        shapes = shapes && (movImmediate || compareImmediate);
    }
    check(shapes, "each is a mov esi/ebx, imm32 (base) or a cmp esi/ebx, imm32 (end)");
    bool pairs = true;
    for (size_t i = 0; i < std::size(kSkinPool); ++i) {
        const bool isEnd = (i % 2) == 1;
        const bool compare = partner_sites::parseHex(kSkinPool[i].expected)[0] == 0x81;
        pairs = pairs && isEnd == compare;
    }
    check(pairs, "the rows alternate base (mov) and end (cmp), as the rule's order has them");
    check(immediateOf(kSkinPool[2]) == kShippedBase + 0x24 && immediateOf(kSkinPool[2]) == immediateOf(kSkinPool[4]) &&
              immediateOf(kSkinPool[3]) == immediateOf(kSkinPool[5]),
          "PV5 and PV7 share the base + 0x24, PV6 and PV8 the end + 0x24");

    std::vector<other_sites::Range> others = other_sites::milestones3to8();
    const auto activities = other_sites::activities();
    const auto crowd = other_sites::players3and4();
    others.insert(others.end(), activities.begin(), activities.end());
    others.insert(others.end(), crowd.begin(), crowd.end());
    bool clear = true;
    bool clearOfJuiced = true;
    bool apart = true;
    for (const auto& site : kSkinPool) {
        const size_t size = partner_sites::parseHex(site.expected).size();
        clear = clear && !other_sites::anyOverlap(others, site.address, size);
        const uintptr_t first = site.address - kJuicedMargin;
        const uintptr_t last = site.address + size + kJuicedMargin;
        clearOfJuiced = clearOfJuiced && !std::any_of(std::begin(juiced::kTargets), std::end(juiced::kTargets), [&](uint32_t target) { return target >= first && target < last; });
        for (const auto& other : kSkinPool) {
            apart = apart && (&other == &site || !other_sites::overlaps(site.address, size, other.address, partner_sites::parseHex(other.expected).size()));
        }
    }
    check(clear && apart, "no skin pool site overlaps another or a site of milestones 3 to 13");
    check(clearOfJuiced, "no skin pool site within 16 bytes of an address Juiced touches");
}

void theCutsceneSites() {
    const auto& visuals = player_sites::kCutsceneVisuals;
    check(std::size(visuals) == 4 && std::string(visuals[0].id) == "PV1" && std::string(visuals[2].id) == "PV2", "PV1 and PV2 with a check of each resume");
    check(visuals[0].kind == partner_sites::Kind::JumpOut && visuals[0].address == 0x6C04FD && partner_sites::parseHex(visuals[0].expected).size() == 7 &&
              visuals[2].kind == partner_sites::Kind::JumpOut && visuals[2].address == 0x6C561B && partner_sites::parseHex(visuals[2].expected).size() == 33,
          "PV1 jumps out of the 7 bytes at 0x6C04FD, PV2 out of the 33 at 0x6C561B");
    check(visuals[1].kind == partner_sites::Kind::Bytes && visuals[1].address == 0x6C0584 && visuals[1].tail.empty() &&
              partner_sites::parseHex(visuals[1].expected) == std::vector<uint8_t>{0x8B, 0x0D, 0xB0, 0x8D, 0xE9, 0x00} &&
              visuals[3].kind == partner_sites::Kind::Bytes && visuals[3].address == 0x6C563C && visuals[3].tail.empty() &&
              partner_sites::parseHex(visuals[3].expected) == std::vector<uint8_t>{0x80, 0xBB, 0xFC, 0x0A, 0x00, 0x00, 0x00},
          "the resume points 0x6C0584 and 0x6C563C are checked and not written");
    check(visuals[0].thunk == partner_sites::Thunk::HideRemotesAtCutsceneStart && visuals[2].thunk == partner_sites::Thunk::RestoreRemotesAtCutsceneEnd,
          "each block has its own thunk");
}

void theHideAndKillRules() {
    using player_visuals_rules::healthIsPositive;
    using player_visuals_rules::needsKill;
    constexpr uint32_t kOne = 0x3F800000;
    constexpr uint32_t kMinusOne = 0xBF800000;
    constexpr uint32_t kPositiveInfinity = 0x7F800000;
    constexpr uint32_t kNegativeInfinity = 0xFF800000;
    constexpr uint32_t kNan = 0x7FC00000;
    constexpr uint32_t kNegativeZero = 0x80000000;
    constexpr uint32_t kSmallest = 1;
    constexpr int32_t kUp = -1;
    constexpr int32_t kDown = 230873;
    check(healthIsPositive(kOne) && healthIsPositive(kSmallest) && healthIsPositive(kPositiveInfinity), "1.0, the smallest denormal and infinity are above 0");
    check(!healthIsPositive(0) && !healthIsPositive(kNegativeZero) && !healthIsPositive(kMinusOne) && !healthIsPositive(kNegativeInfinity),
          "0, -0, -1.0 and -infinity are not");
    check(healthIsPositive(kNan), "a NaN counts as above 0, as the x87 compare flags say");
    check(!needsKill(true, kOne, kUp), "a live human that resolves and is not down is left hidden, not killed");
    check(needsKill(false, kOne, kUp), "a human whose handle does not resolve to it is killed off");
    check(needsKill(true, 0, kUp) && needsKill(true, kMinusOne, kUp), "a human with no health left is");
    check(needsKill(true, kOne, kDown) && needsKill(true, kOne, 0), "a human that is down (timer 0 or more) is");
    check(player_visuals_rules::humansText(nullptr, 0).empty(), "no human, no text");
    const uintptr_t humans[] = {0x2CD1E80, 0x2CD3770};
    check(player_visuals_rules::humansText(humans, 2) == "0x2CD1E80, 0x2CD3770", "the humans as the trace lines name them");
}

}  // namespace

int main() {
    theSkinPoolImmediates();
    theSkinPoolSites();
    theCutsceneSites();
    theHideAndKillRules();
    return g_failures == 0 ? 0 : 1;
}
