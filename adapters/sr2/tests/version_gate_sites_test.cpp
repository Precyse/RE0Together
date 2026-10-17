// The milestone 8 and 11 B3 sites as data (no game): the checked bytes, what is written, and clearance from every other patch site
// and from everything Juiced touches.
#include <algorithm>
#include <cstdio>

#include "../src/game_port_patches.h"
#include "../src/join_net_trace_sites.h"
#include "../src/lan_address_patches.h"
#include "../src/partner_sites.h"
#include "../src/player_cap_patches.h"
#include "../src/replication_sites.h"
#include "../src/version_gate_patches.h"
#include "juiced_targets.h"

namespace {

constexpr uintptr_t kGateReq = 0x10007000;
constexpr uintptr_t kGateAcc = 0x10007100;
constexpr uintptr_t kSend = 0x10007200;
constexpr uintptr_t kNoteDeny = 0x10007300;
constexpr uintptr_t kRefusalText = 0x10007400;
constexpr uintptr_t kOtherThunk = 0x10002000;
constexpr uintptr_t kJuicedMargin = 16;
constexpr uint32_t kFourPlayers = 4;
constexpr uint32_t kMovedPort = 4201;
constexpr size_t kSites = 12;

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_failures;
}

bool overlaps(uintptr_t a, size_t aSize, uintptr_t b, size_t bSize) { return a < b + bSize && b < a + aSize; }

const CodePatch* patchAt(const std::vector<CodePatch>& patches, uintptr_t address) {
    for (const CodePatch& patch : patches) {
        if (patch.address == address) return &patch;
    }
    return nullptr;
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

// Every other edit of the adapter, as (address, size).
std::vector<std::pair<uintptr_t, size_t>> otherSites() {
    std::vector<std::pair<uintptr_t, size_t>> sites;
    for (const partner_sites::Site& site : partner_sites::kSites) {
        sites.push_back({site.address, partner_sites::parseHex(site.expected).size()});
    }
    for (const auto& entry : replication_sites::kEntries) sites.push_back({entry.n1, replication_sites::kEntryProbeBytes});
    for (const auto& branch : replication_sites::kBranches) sites.push_back({branch.address, 2});
    for (const auto& trace : join_net_trace_sites::kSites) {
        sites.push_back({trace.address, partner_sites::parseHex(trace.expected).size()});
    }
    const player_cap_patches::Thunks capThunks{kOtherThunk, kOtherThunk, kOtherThunk};
    const game_port_patches::Thunks portThunks{kOtherThunk, kOtherThunk, kOtherThunk, kOtherThunk};
    for (const auto& list : {player_cap_patches::forPlayers(kFourPlayers, capThunks),
                             game_port_patches::forPort(kMovedPort, kFourPlayers, true, Transport::Native, portThunks),
                             lan_address_patches::forPlayers(kFourPlayers, kOtherThunk)}) {
        for (const CodePatch& patch : list) sites.push_back({patch.address, patch.expected.size()});
    }
    return sites;
}

bool clearOfOtherSites(const std::vector<CodePatch>& patches) {
    const auto others = otherSites();
    for (const CodePatch& patch : patches) {
        if (patch.replacement.empty()) continue;  // a function entry the gates call: shared with the trace hooks on purpose
        for (const auto& [address, size] : others) {
            if (overlaps(patch.address, patch.expected.size(), address, size)) return false;
        }
    }
    return true;
}

}  // namespace

int main() {
    const auto patches = version_gate_patches::patches({kGateReq, kGateAcc, kSend, kNoteDeny, kRefusalText});
    check(patches.size() == kSites, "twelve sites: three table pointers, three calls and six function entries");

    const CodePatch* req = patchAt(patches, 0xE8BF3C);
    check(req && req->expected == std::vector<uint8_t>{0xC0, 0x5B, 0x81, 0x00} &&
              req->replacement == std::vector<uint8_t>{0x00, 0x70, 0x00, 0x10},
          "V1: the join_req handler pointer 0x815BC0 in the message table becomes the gate");
    const CodePatch* acc = patchAt(patches, 0xE8BF54);
    check(acc && acc->expected == std::vector<uint8_t>{0xE0, 0x62, 0x81, 0x00} &&
              acc->replacement == std::vector<uint8_t>{0x00, 0x71, 0x00, 0x10},
          "V2: the join_acc handler pointer 0x8162E0 becomes the gate");

    const CodePatch* deny = patchAt(patches, 0xE8BF6C);
    check(deny && deny->expected == std::vector<uint8_t>{0xF0, 0x63, 0x81, 0x00} &&
              deny->replacement == std::vector<uint8_t>{0x00, 0x73, 0x00, 0x10},
          "V9: the join_deny handler pointer 0x8163F0 becomes the deny note");

    const auto callTo = [](uintptr_t address, uintptr_t thunk) {
        const uint32_t to = static_cast<uint32_t>(thunk - (address + 5));
        return std::vector<uint8_t>{0xE8, static_cast<uint8_t>(to), static_cast<uint8_t>(to >> 8), static_cast<uint8_t>(to >> 16),
                                    static_cast<uint8_t>(to >> 24)};
    };
    const CodePatch* sendReq = patchAt(patches, 0x815B9D);
    const CodePatch* sendAcc = patchAt(patches, 0x8162BB);
    check(sendReq && sendReq->expected == std::vector<uint8_t>{0xE8, 0xCE, 0xEA, 0xFF, 0xFF} &&
              sendReq->replacement == callTo(0x815B9D, kSend),
          "V3: the send in the join_req sender (call 0x814670) goes to the trailer thunk");
    check(sendAcc && sendAcc->expected == std::vector<uint8_t>{0xE8, 0xB0, 0xE3, 0xFF, 0xFF} &&
              sendAcc->replacement == callTo(0x8162BB, kSend),
          "V4: the send in the join_acc sender goes to the same thunk");

    const CodePatch* text = patchAt(patches, 0x81ECE7);
    check(text && text->expected == std::vector<uint8_t>{0xE8, 0x64, 0xFD, 0xFF, 0xFF} && text->replacement == callTo(0x81ECE7, kRefusalText),
          "VG1: the call of the error text function 0x81EA50 in the join error popup goes to the refusal thunk");

    for (const uintptr_t entry : {0x816F60u, 0x8163F0u, 0x815BC0u, 0x8162E0u, 0x814790u, 0x81EA50u}) {
        const CodePatch* patch = patchAt(patches, entry);
        check(patch && patch->expected.size() == 8 && patch->replacement.empty(), "an entry the gates call is only checked");
    }
    check(patchAt(patches, 0x815BC0)->expected == std::vector<uint8_t>{0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8, 0x81, 0xEC} &&
              patchAt(patches, 0x816F60)->expected == std::vector<uint8_t>{0x81, 0xEC, 0x24, 0x05, 0x00, 0x00, 0xA1, 0x50},
          "V7 and V5: the bytes of the join_req handler and the join_deny sender");
    check(patchAt(patches, 0x814790)->expected == std::vector<uint8_t>{0x81, 0xEC, 0x10, 0x02, 0x00, 0x00, 0xA1, 0x50} &&
              patchAt(patches, 0x81EA50)->expected == std::vector<uint8_t>{0x83, 0xC1, 0xFF, 0x33, 0xC0, 0x83, 0xF9, 0x0C},
          "VG2 and the error text function: the bytes of the send to an address and of the code to text function");
    check(version_gate_patches::kJoinDenyHeaderFlag == 0xE8BF7C && version_gate_patches::kJoinRejectedCode == 8,
          "the join_deny header flag byte and the join rejected code");
    check(clearOfJuiced(patches), "no site within 16 bytes of an address Juiced touches");
    check(clearOfOtherSites(patches), "no written site overlaps a milestone 3, 4, 5, 5b, 5e, 5g or 5h site");
    return g_failures == 0 ? 0 : 1;
}
