// Milestones 5e and 5g (no game): the port rules, the code edits as data, and clearance from the other patch sites and from
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
constexpr uintptr_t kReportThunk = 0x10004100;
constexpr uintptr_t kRelayThunk = 0x10004200;
constexpr uintptr_t kTraceThunk = 0x10004300;
const game_port_patches::Thunks kThunks{kThunk, kReportThunk, kRelayThunk, kTraceThunk};
constexpr uintptr_t kOtherThunk = 0x10002000;
constexpr uintptr_t kJuicedMargin = 16;
constexpr uint32_t kFourPlayers = 4;
constexpr uint32_t kMovedPort = 4201;
constexpr size_t kAllSites = 10;  // E1, six E2, E3, H1, H2
constexpr size_t kTracedSites = 11;  // and T11

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
    const player_cap_patches::Thunks thunks{kOtherThunk, kOtherThunk};
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

    const auto moved = game_port_patches::forPort(kMovedPort, kFourPlayers, false, kThunks);
    check(moved.size() == kAllSites, "4 players and a moved port: ten sites");
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
    const auto callTo = [](uintptr_t address, uintptr_t thunk) {
        const uint32_t to = static_cast<uint32_t>(thunk - (address + 5));
        return std::vector<uint8_t>{0xE8, static_cast<uint8_t>(to), static_cast<uint8_t>(to >> 8), static_cast<uint8_t>(to >> 16),
                                    static_cast<uint8_t>(to >> 24)};
    };
    const CodePatch* report = patchAt(moved, 0x815D8C);
    std::vector<uint8_t> reportCall = callTo(0x815D8C, kReportThunk);
    reportCall.push_back(0x90);
    check(report && report->expected == std::vector<uint8_t>{0x8B, 0x44, 0x24, 0x14, 0x8B, 0x00} &&
              report->replacement == reportCall,
          "H1: 8B 44 24 14 8B 00 is checked, it becomes call rel32 to the capture thunk and a nop");
    const CodePatch* relay = patchAt(moved, 0x815666);
    check(relay && relay->expected == std::vector<uint8_t>{0xE8, 0x65, 0x5D, 0xFE, 0xFF} &&
              relay->replacement == callTo(0x815666, kRelayThunk),
          "H2: call 0x7FB3D0 (E8 65 5D FE FF) goes to the record port thunk");
    check(!patchAt(moved, 0x813BF2), "T11 is not in the list without trace");
    check(std::all_of(moved.begin(), moved.end(), [](const CodePatch& p) { return p.replacement.size() <= p.expected.size(); }),
          "no replacement is longer than the bytes checked");

    const auto shipped = game_port_patches::forPort(4200, kFourPlayers, false, kThunks);
    check(shipped.size() == kAllSites && patchAt(shipped, 0xC1BBAF)->replacement.empty() &&
              patchAt(shipped, 0x811B11)->replacement.empty() && !patchAt(shipped, 0x8CDA3B)->replacement.empty(),
          "port 4200 with 4 players: every site checked, E1 and E2 not written");
    check(!patchAt(shipped, 0x815D8C)->replacement.empty() && !patchAt(shipped, 0x815666)->replacement.empty(),
          "port 4200 with 4 players: H1 and H2 are still written (the local port is relayed whatever it is)");

    const auto traced = game_port_patches::forPort(kMovedPort, kFourPlayers, true, kThunks);
    const CodePatch* unmapped = patchAt(traced, 0x813BF2);
    check(traced.size() == kTracedSites && unmapped && unmapped->expected == std::vector<uint8_t>{0xE8, 0x79, 0x64, 0xFE, 0xFF} &&
              unmapped->replacement == callTo(0x813BF2, kTraceThunk),
          "trace=1: T11, call 0x7FA070 (E8 79 64 FE FF) goes to the unmapped sender trace");
    const auto traceOnly = game_port_patches::forPort(4200, 2, true, kThunks);
    check(traceOnly.size() == 2 && traceOnly[0].replacement.empty() && !traceOnly[1].replacement.empty(),
          "trace=1 at 2 players and port 4200: E1 checked, T11 written");

    const auto twoPlayers = game_port_patches::forPort(kMovedPort, 2, false, kThunks);
    check(twoPlayers.size() == 1 && twoPlayers[0].address == 0xC1BBAF && !twoPlayers[0].replacement.empty(),
          "2 players and a moved port: only E1 (E2 and E3 follow the max_players gate)");
    check(game_port_patches::forPort(4200, 2, false, kThunks).empty(), "port 4200 with 2 players and no trace: nothing at all");

    check(disjoint(traced), "no two sites overlap");
    check(clearOfJuiced(traced), "no site within 16 bytes of an address Juiced touches");
    check(clearOfOtherMilestones(traced), "no site overlaps a milestone 3, 4, 5, 5b or 5d site");
    return g_failures == 0 ? 0 : 1;
}
