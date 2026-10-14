// Milestone 5i (no game): the tables cleared for a pool index, the trace line, and the two sites as data.
#include <algorithm>
#include <cstdio>

#include "../src/look_reset_rules.h"
#include "../src/partner_sites.h"
#include "../src/player_cap_patches.h"
#include "juiced_targets.h"

namespace {

using look_reset_rules::Range;

constexpr uintptr_t kThunk = 0x10002000;
constexpr uintptr_t kResetThunk = 0x10008000;
constexpr uintptr_t kTraceThunk = 0x10008100;
constexpr uintptr_t kJuicedMargin = 16;
constexpr uint32_t kFourPlayers = 4;

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_failures;
}

const CodePatch* patchAt(const std::vector<CodePatch>& patches, uintptr_t address) {
    for (const CodePatch& patch : patches) {
        if (patch.address == address) return &patch;
    }
    return nullptr;
}

std::vector<uint8_t> callTo(uintptr_t address, uintptr_t thunk) {
    const uint32_t to = static_cast<uint32_t>(thunk - (address + 5));
    return {0xE8, static_cast<uint8_t>(to), static_cast<uint8_t>(to >> 8), static_cast<uint8_t>(to >> 16),
            static_cast<uint8_t>(to >> 24)};
}

}  // namespace

int main() {
    using namespace look_reset_rules;

    const std::vector<Range> first = rangesFor(0);
    check(first.size() == 6, "six tables are cleared for an index");
    check(first[0] == Range{0x1FC8C90, 4} && first[1] == Range{0x1FC8CC0, 0x80}, "applied tints: the count and the 0x80 byte row of index 0");
    check(first[2] == Range{0x1FC8660, 4} && first[3] == Range{0x1FC8690, 0x80}, "pending tints: the count and the row");
    check(first[4] == Range{0x1FC85A0, 4} && first[5] == Range{0x1FC85D0, 12}, "pending body morph: the count and three dwords");

    const std::vector<Range> second = rangesFor(1);
    check(second[0].address == 0x1FC8C94 && second[1].address == 0x1FC8D40 && second[3].address == 0x1FC8710 &&
              second[4].address == 0x1FC85A4 && second[5].address == 0x1FC85DC,
          "index 1: the count at +4, a tint row +0x80, a morph value +0xC");
    const std::vector<Range> last = rangesFor(11);
    check(last[1].address + last[1].size == 0x1FC8CC0 + 12 * 0x80, "index 11 ends the applied tint table");
    check(rangesFor(12).empty() && rangesFor(0xFFFFFFFF).empty(), "an index outside the tables clears nothing");
    check(last[0].address + 4 == kAppliedEntries && last[2].address + 4 == 0x1FC8690,
          "the count tables end where their entry tables start (12 indices of 4 bytes)");

    check(describeSend(1, {}) == "looks: send 0 body tints from index 1:", "a send without tints");
    check(describeSend(2, {{3, 0xFF00FF00, 0x11}, {4, 1, 2}}) ==
              "looks: send 2 body tints from index 2: slot 3 FF00FF00/00000011 slot 4 00000001/00000002",
          "a send lists each slot with its two tints");

    const player_cap_patches::Thunks thunks{kThunk, kThunk, kThunk, kResetThunk, kTraceThunk};
    const auto patches = player_cap_patches::forPlayers(kFourPlayers, thunks);
    const CodePatch* reset = patchAt(patches, 0x9D4379);
    check(reset && reset->expected == std::vector<uint8_t>{0xE8, 0x22, 0xF8, 0x00, 0x00} &&
              reset->replacement == callTo(0x9D4379, kResetThunk),
          "A1: call 0x9E3BA0 (E8 22 F8 00 00) at 0x9D4379 goes to the reset thunk");
    check(!patchAt(patches, 0x82A17C), "T1 is not written without trace");
    const auto traced = player_cap_patches::forPlayers(kFourPlayers, thunks, true);
    const CodePatch* send = patchAt(traced, 0x82A17C);
    check(send && send->expected == std::vector<uint8_t>{0xE8, 0x3F, 0xE3, 0xFA, 0xFF} &&
              send->replacement == callTo(0x82A17C, kTraceThunk),
          "T1 (trace): call 0x7D84C0 (E8 3F E3 FA FF) at 0x82A17C goes to the trace thunk");
    check(player_cap_patches::forPlayers(2, thunks, true).empty(), "max_players 2 patches nothing");

    bool clearOfJuiced = true;
    for (const CodePatch& patch : traced) {
        const uintptr_t low = patch.address - kJuicedMargin;
        const uintptr_t high = patch.address + patch.expected.size() + kJuicedMargin;
        clearOfJuiced = clearOfJuiced && std::none_of(std::begin(juiced::kTargets), std::end(juiced::kTargets),
                                                      [&](uint32_t target) { return target >= low && target < high; });
    }
    check(clearOfJuiced, "no site within 16 bytes of an address Juiced touches");
    return g_failures == 0 ? 0 : 1;
}
