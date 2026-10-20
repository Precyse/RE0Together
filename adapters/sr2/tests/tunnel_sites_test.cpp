// The Steam tunnel's exe edits as data (no game): the bytes expected and written at each site, an empty table for the native
// transport, and clearance from every other patch site and from everything Juiced touches.
#include <algorithm>
#include <cstdio>
#include <set>
#include <string_view>

#include "../src/tunnel_sites.h"
#include "check.h"
#include "juiced_targets.h"
#include "other_sites.h"
#include "site_lookup.h"

namespace {

constexpr uintptr_t kSendThunk = 0x10008000;
constexpr uintptr_t kSelectThunk = 0x10008100;
constexpr uintptr_t kReceiveThunk = 0x10008200;
constexpr uintptr_t kHostByNameThunk = 0x10008300;
constexpr tunnel_sites::Thunks kThunks{kSendThunk, kSelectThunk, kReceiveThunk, kHostByNameThunk};
constexpr uintptr_t kJuicedMargin = 16;
constexpr uintptr_t kTunnelMargin = 0x100;  // from the five call retargets S1 to S5
constexpr size_t kExpectedSites = 21;       // S1..S6, A1, five L1, four function entries, five LAN entries
constexpr size_t kWrittenSites = 10;        // S1, S2, S4, S5, A1 and the five L1 calls

std::vector<uint8_t> callTo(uintptr_t from, uintptr_t to) {
    const auto call = relativeBranch(kCallRel32, from, to);
    return {call.begin(), call.end()};
}

bool isL1(const CodePatch& patch) { return std::string_view(patch.name) == "host_by_name"; }

// S1 to S6 (the A1 branch is the other "tunnel_" row).
bool isSocketCall(const CodePatch& patch) {
    const std::string_view name = patch.name;
    return name.starts_with("tunnel_") && name != "tunnel_public_address";
}

bool uniqueAndDisjoint(const std::vector<CodePatch>& patches) {
    std::set<std::string_view> names;
    for (const CodePatch& a : patches) {
        if (!isL1(a) && !names.insert(a.name).second) return false;
        for (const CodePatch& b : patches) {
            if (&a != &b && other_sites::overlaps(a.address, a.expected.size(), b.address, b.expected.size())) return false;
        }
    }
    return true;
}

bool clearOfJuiced(const std::vector<CodePatch>& patches) {
    for (const CodePatch& patch : patches) {
        const uintptr_t first = patch.address - kJuicedMargin;
        const uintptr_t last = patch.address + patch.expected.size() + kJuicedMargin;
        if (std::any_of(std::begin(juiced::kTargets), std::end(juiced::kTargets),
                        [&](uint32_t target) { return target >= first && target < last; })) {
            std::printf("     %s at 0x%X is within %u bytes of a Juiced target\n", patch.name, static_cast<unsigned>(patch.address),
                        static_cast<unsigned>(kJuicedMargin));
            return false;
        }
    }
    return true;
}

// The edits of every other milestone (the five L1 calls are this table's own).
std::vector<other_sites::Range> otherMilestones() {
    std::vector<other_sites::Range> others = other_sites::milestones3to8();
    for (const auto& more : {other_sites::activities(), other_sites::players3and4()}) others.insert(others.end(), more.begin(), more.end());
    return others;
}

bool clearOfOtherMilestones(const std::vector<CodePatch>& patches) {
    const auto others = otherMilestones();
    for (const CodePatch& patch : patches) {
        if (isL1(patch)) continue;  // the same five calls as lan_address's table, which is not installed with the tunnel
        const uintptr_t margin = isSocketCall(patch) ? kTunnelMargin : 0;
        if (other_sites::anyOverlap(others, patch.address - margin, patch.expected.size() + 2 * margin)) {
            std::printf("     %s at 0x%X is too close to another milestone's site\n", patch.name, static_cast<unsigned>(patch.address));
            return false;
        }
    }
    return true;
}

}  // namespace

int main() {
    check(tunnel_sites::patches(Transport::Native, kThunks).empty(), "native transport: no site is checked or written");

    const auto patches = tunnel_sites::patches(Transport::Steam, kThunks);
    check(patches.size() == kExpectedSites, "steam transport: 21 rows");

    const CodePatch* s1 = patchAt(patches, 0xC1B5A2);
    check(s1 && s1->expected == std::vector<uint8_t>{0xE8, 0xC7, 0x7F, 0x00, 0x00} && s1->replacement == callTo(0xC1B5A2, kSendThunk),
          "S1: call sendto (E8 C7 7F 00 00) at 0xC1B5A2 goes to the tunnel's send");
    const CodePatch* s2 = patchAt(patches, 0xC1BD73);
    check(s2 && s2->expected == std::vector<uint8_t>{0xE8, 0x20, 0x78, 0x00, 0x00} && s2->replacement == callTo(0xC1BD73, kSelectThunk),
          "S2: call select (E8 20 78 00 00) at 0xC1BD73 goes to the readable check");
    const CodePatch* s3 = patchAt(patches, 0xC1BD89);
    check(s3 && s3->expected == std::vector<uint8_t>{0xE8, 0x10, 0x78, 0x00, 0x00} && s3->replacement.empty(),
          "S3: call __WSAFDIsSet (E8 10 78 00 00) at 0xC1BD89 is only checked");
    const CodePatch* s4 = patchAt(patches, 0xC1BDBC);
    check(s4 && s4->expected == std::vector<uint8_t>{0xE8, 0xB9, 0x77, 0x00, 0x00} && s4->replacement == callTo(0xC1BDBC, kReceiveThunk),
          "S4: call recvfrom (E8 B9 77 00 00) at 0xC1BDBC goes to the tunnel's receive");
    const CodePatch* s5 = patchAt(patches, 0x814375);
    check(s5 && s5->expected == std::vector<uint8_t>{0xE8, 0x1E, 0xF2, 0x40, 0x00} && s5->replacement == callTo(0x814375, kSelectThunk),
          "S5: call select (E8 1E F2 40 00) at 0x814375 goes to the same readable check");
    const CodePatch* s6 = patchAt(patches, 0x81438A);
    check(s6 && s6->expected == std::vector<uint8_t>{0xE8, 0x0F, 0xF2, 0x40, 0x00} && s6->replacement.empty(),
          "S6: call __WSAFDIsSet (E8 0F F2 40 00) at 0x81438A is only checked");
    const CodePatch* a1 = patchAt(patches, 0x810CAF);
    check(a1 && a1->expected == std::vector<uint8_t>{0x74, 0x69} && a1->replacement == std::vector<uint8_t>{0xEB, 0x69},
          "A1: je 0x810D1A (74 69) becomes jmp (EB 69): the public address is the private one");

    const std::pair<uintptr_t, std::vector<uint8_t>> l1[] = {
        {0x810C94, {0xE8, 0xAB, 0x28, 0x41, 0x00}}, {0x7F79D0, {0xE8, 0x6F, 0xBB, 0x42, 0x00}},
        {0x8E1B3E, {0xE8, 0x01, 0x1A, 0x34, 0x00}}, {0x8D7C00, {0xE8, 0x3F, 0xB9, 0x34, 0x00}},
        {0xC1C0FE, {0xE8, 0x41, 0x74, 0x00, 0x00}},
    };
    bool l1Right = true;
    for (const auto& [address, bytes] : l1) {
        const CodePatch* patch = patchAt(patches, address);
        l1Right = l1Right && patch && patch->expected == bytes && patch->replacement == callTo(address, kHostByNameThunk);
    }
    check(l1Right, "L1a..e: the five gethostbyname calls (0x810C94, 0x7F79D0, 0x8E1B3E, 0x8D7C00, 0xC1C0FE) go to the own address thunk");

    const std::pair<uintptr_t, std::vector<uint8_t>> entries[] = {
        {0xC1B540, {0x81, 0xEC, 0x30, 0x05, 0x00, 0x00, 0xA1, 0x50}}, {0xC1BD10, {0x81, 0xEC, 0x5C, 0x06, 0x00, 0x00, 0xA1, 0x50}},
        {0x814320, {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8, 0x81, 0xEC}}, {0x810C70, {0x83, 0xEC, 0x58, 0xA1, 0x50, 0x0B, 0xE8, 0x00}},
        {0x7EE710, {0x80, 0x3D, 0xBC, 0x7E, 0x52, 0x02, 0x00, 0x75, 0x05, 0xE8, 0x82, 0x47, 0x02, 0x00}},
        {0x7EE730, {0x80, 0x3D, 0xBC, 0x7E, 0x52, 0x02, 0x01, 0x75, 0x05, 0xE8, 0x02, 0x49, 0x02, 0x00}},
        {0x813160, {0x8B, 0x0D, 0xF4, 0x8B, 0x52, 0x02, 0x53, 0x33, 0xDB, 0x3B, 0xCB, 0x56}},
        {0x812F80, {0xA1, 0xC0, 0x1F, 0xEB, 0x00, 0x85, 0xC0, 0x56}}, {0x7EE12B, {0x8B, 0x40, 0x0C, 0x38, 0x48, 0x47, 0x74, 0x0A}},
    };
    bool entriesRight = true;
    for (const auto& [address, bytes] : entries) {
        const CodePatch* patch = patchAt(patches, address);
        entriesRight = entriesRight && patch && patch->expected == bytes && patch->replacement.empty();
    }
    check(entriesRight, "F: the entries of psnet send, pump, net thread, own address and the LAN functions are only checked");

    const size_t written = std::count_if(patches.begin(), patches.end(), [](const CodePatch& patch) { return !patch.replacement.empty(); });
    check(written == kWrittenSites, "10 rows are written: S1, S2, S4, S5, A1 and the five L1 calls");
    check(std::all_of(patches.begin(), patches.end(), [](const CodePatch& patch) {
              return patch.replacement.size() <= patch.expected.size();
          }),
          "no replacement is longer than the bytes checked");

    check(uniqueAndDisjoint(patches), "names are unique and no two rows overlap");
    check(clearOfJuiced(patches), "no row within 16 bytes of an address Juiced touches");
    check(clearOfOtherMilestones(patches), "S1 to S5 are 0x100 clear of every other milestone's site, the rest do not overlap one");
    return g_failures == 0 ? 0 : 1;
}
