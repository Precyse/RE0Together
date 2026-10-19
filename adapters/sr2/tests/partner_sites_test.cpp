// The milestone 4 site table (no game): shape, overlaps, and distance from everything Juiced touches.
#include <algorithm>
#include <cstdio>
#include <set>
#include <string_view>

#include "../src/partner_replacement.h"
#include "../src/partner_sites.h"
#include "../src/replication_sites.h"
#include "juiced_targets.h"

namespace {

using partner_sites::Kind;
using partner_sites::Site;

constexpr size_t kExpectedSites = 55;
constexpr uintptr_t kJuicedMargin = 16;
constexpr size_t kBranchBytes = 5;
constexpr uint8_t kCallOpcode = 0xE8;
constexpr uint8_t kJmpOpcode = 0xE9;

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_failures;
}

size_t sizeOf(const Site& site) { return partner_sites::parseHex(site.expected).size(); }

bool addressesAreUnique() {
    std::set<uintptr_t> seen;
    for (const Site& site : partner_sites::kSites) {
        if (!seen.insert(site.address).second) return false;
    }
    return true;
}

bool idsAreUnique() {
    std::set<std::string_view> seen;
    for (const Site& site : partner_sites::kSites) {
        if (!seen.insert(site.id).second) return false;
    }
    return true;
}

bool rangesDoNotOverlap() {
    for (const Site& a : partner_sites::kSites) {
        for (const Site& b : partner_sites::kSites) {
            if (&a != &b && a.address < b.address + sizeOf(b) && b.address < a.address + sizeOf(a)) return false;
        }
    }
    return true;
}

bool clearOfJuiced() {
    for (const Site& site : partner_sites::kSites) {
        const uintptr_t first = site.address - kJuicedMargin;
        const uintptr_t last = site.address + sizeOf(site) + kJuicedMargin;
        const bool hit = std::any_of(std::begin(juiced::kTargets), std::end(juiced::kTargets),
                                     [&](uint32_t target) { return target >= first && target < last; });
        if (hit) {
            std::printf("     %s at 0x%X is within %u bytes of a Juiced target\n", site.id,
                        static_cast<unsigned>(site.address), static_cast<unsigned>(kJuicedMargin));
            return false;
        }
    }
    return true;
}

bool clearOfReplicationSites() {
    for (const Site& site : partner_sites::kSites) {
        const uintptr_t end = site.address + sizeOf(site);
        for (const auto& entry : replication_sites::kEntries) {
            if (entry.n1 < end && site.address < entry.n1 + replication_sites::kEntryProbeBytes) return false;
        }
        for (const auto& branch : replication_sites::kBranches) {
            if (branch.address < end && site.address < branch.address + 2) return false;
        }
    }
    return true;
}

bool retargetsStartWithABranch() {
    for (const Site& site : partner_sites::kSites) {
        if (site.kind != Kind::RetargetCall) continue;
        const auto bytes = partner_sites::parseHex(site.expected);
        if (bytes.size() != kBranchBytes || (bytes[0] != kCallOpcode && bytes[0] != kJmpOpcode)) return false;
    }
    return true;
}

bool blocksHaveRoomForTheirBranch() {
    for (const Site& site : partner_sites::kSites) {
        if (site.kind != Kind::CallBlock && site.kind != Kind::JumpOut) continue;
        if (sizeOf(site) < kBranchBytes) return false;
        if (site.kind == Kind::CallBlock && sizeOf(site) < kBranchBytes + partner_sites::parseHex(site.tail).size()) return false;
        if (site.kind == Kind::JumpOut && !site.tail.empty()) return false;
    }
    return true;
}

bool tailsOnlyOnCallBlocks() {
    for (const Site& site : partner_sites::kSites) {
        if (site.kind != Kind::CallBlock && !site.tail.empty()) return false;
    }
    return true;
}

bool hooksCoverFiveBytes() {
    for (const Site& site : partner_sites::kSites) {
        if (site.kind == Kind::HookEntry && sizeOf(site) < kBranchBytes) return false;
    }
    return true;
}

const Site* siteNamed(std::string_view id) {
    for (const Site& site : partner_sites::kSites) {
        if (id == site.id) return &site;
    }
    return nullptr;
}

bool replacementsFitTheirSites() {
    for (const Site& site : partner_sites::kSites) {
        const auto bytes = partner_replacement::replacementFor(site, 0x10000000);
        if (bytes.size() > sizeOf(site)) return false;
        if (site.kind == Kind::HookEntry ? !bytes.empty() : bytes.size() < kBranchBytes) return false;
    }
    return true;
}

bool replacementsHaveTheDocumentedShape() {
    const std::vector<uint8_t> handoff = partner_replacement::replacementFor(*siteNamed("O3"), 0x8069B8 + 5 + 0x1000);
    const std::vector<uint8_t> tailJump = partner_replacement::replacementFor(*siteNamed("E5"), 0x7CE304 + 5);
    const std::vector<uint8_t> leave = partner_replacement::replacementFor(*siteNamed("L1"), 0x8CB39E + 5);
    return handoff == std::vector<uint8_t>{0xE8, 0x00, 0x10, 0x00, 0x00, 0xEB, 0x1B, 0x90} &&
           tailJump == std::vector<uint8_t>{0xE9, 0x00, 0x00, 0x00, 0x00} &&
           leave == std::vector<uint8_t>{0xE9, 0x00, 0x00, 0x00, 0x00, 0x90, 0x90, 0x90, 0x90, 0x90};
}

bool hexParses() {
    const auto bytes = partner_sites::parseHex("8B 80 00 0a ff");
    return bytes == std::vector<uint8_t>{0x8B, 0x80, 0x00, 0x0A, 0xFF} && partner_sites::parseHex("").empty();
}

}  // namespace

int main() {
    check(partner_sites::kSiteCount == kExpectedSites, "55 sites");
    check(hexParses(), "hex text parses");
    check(idsAreUnique(), "ids are unique");
    check(addressesAreUnique(), "addresses are unique");
    check(rangesDoNotOverlap(), "no two sites overlap");
    check(retargetsStartWithABranch(), "a retargeted call is a 5 byte call or jmp");
    check(blocksHaveRoomForTheirBranch(), "a block has room for its branch (and its tail)");
    check(tailsOnlyOnCallBlocks(), "only a call block has a tail");
    check(replacementsFitTheirSites(), "every replacement fits its site, a hook writes nothing itself");
    check(replacementsHaveTheDocumentedShape(), "O3 call + jmp short + nop, E5 stays a jmp, L1 jmp + 5 nops");
    check(hooksCoverFiveBytes(), "a hook covers the 5 bytes MinHook overwrites");
    check(clearOfJuiced(), "no site within 16 bytes of an address Juiced touches");
    check(clearOfReplicationSites(), "no site overlaps a milestone 3 site");
    return g_failures == 0 ? 0 : 1;
}
