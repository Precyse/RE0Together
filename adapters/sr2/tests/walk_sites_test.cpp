// The milestone 17 wave 3 site tables (no game): the row counts of every set, the shapes of each kind, where the written calls and
// jumps land, the functions the replaced calls name, and clearance from every other milestone's writes and from everything Juiced touches.
#include <algorithm>
#include <cstdio>
#include <iterator>
#include <set>
#include <span>
#include <string_view>
#include <vector>

#include "../src/activity_sites.h"
#include "../src/diversion_sites.h"
#include "../src/partner_replacement.h"
#include "../src/walk_sites.h"
#include "juiced_targets.h"
#include "other_sites.h"

namespace {

using partner_sites::Gate;
using partner_sites::Kind;
using partner_sites::Site;

constexpr uintptr_t kJuicedMargin = 16;
constexpr size_t kBranchBytes = 5;
constexpr uint8_t kShortJump = 0xEB;
constexpr uintptr_t kShortJumpBytes = 2;
constexpr uintptr_t kNearJumpIfNotEqualBytes = 6;  // 0F 85 rel32

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_failures;
}

size_t sizeOf(const Site& site) { return partner_sites::parseHex(site.expected).size(); }

std::vector<Site> allSites() {
    std::vector<Site> all;
    for (const walk_sites::Set& set : walk_sites::kSets) all.insert(all.end(), set.sites.begin(), set.sites.end());
    return all;
}

const Site* siteNamed(const std::vector<Site>& sites, std::string_view id) {
    for (const Site& site : sites) {
        if (id == site.id) return &site;
    }
    return nullptr;
}

uint32_t dwordOf(const std::vector<uint8_t>& bytes, size_t at) {
    return bytes[at] | bytes[at + 1] << 8 | bytes[at + 2] << 16 | static_cast<uint32_t>(bytes[at + 3]) << 24;
}

// Where the `call rel32` or `jmp rel32` at `at` bytes into the checked bytes of `site` goes.
uintptr_t branchTarget(const Site& site, size_t at) {
    return site.address + at + kBranchBytes + dwordOf(partner_sites::parseHex(site.expected), at + 1);
}

void theCounts() {
    struct Count {
        const char* set;
        size_t rows;
    };
    constexpr Count kCounts[] = {{"fraud walks", 7},  {"snatch walks", 4},     {"trafficking walks", 2}, {"mayhem walks", 1},
                                 {"derby walks", 4}, {"fight club walks", 2}, {"escort walks", 3},      {"mayhem blocks", 5}};
    bool same = std::size(kCounts) == std::size(walk_sites::kSets);
    for (size_t i = 0; same && i < std::size(kCounts); ++i) {
        same = std::string_view(walk_sites::kSets[i].name) == kCounts[i].set && walk_sites::kSets[i].sites.size() == kCounts[i].rows;
    }
    check(same, "the sets: fraud 7, snatch 4, trafficking 2, mayhem 1, derby 4, fight club 2, escort 3, mayhem blocks 5");
    check(allSites().size() == 28, "28 rows in all");
    bool allNow = true;
    for (const Site& site : allSites()) allNow = allNow && site.gate == Gate::Now;
    check(allNow, "no row waits for partner_experimental");
}

void theShapes() {
    bool fit = true;
    bool sameLength = true;
    for (const Site& site : allSites()) {
        const auto bytes = partner_sites::parseHex(site.expected);
        if (site.kind == Kind::RetargetCall && (bytes.size() != kBranchBytes || bytes[0] != 0xE8 || !site.tail.empty())) fit = false;
        if (site.kind == Kind::JumpOut && (bytes.size() < kBranchBytes || !site.tail.empty())) fit = false;
        if (site.kind == Kind::CallBlock && bytes.size() != kBranchBytes + partner_sites::parseHex(site.tail).size()) fit = false;
        if (site.kind == Kind::HookEntry || site.kind == Kind::Bytes || site.kind == Kind::PointerSwap) fit = false;
        sameLength = sameLength && partner_replacement::replacementFor(site, 0x10000000).size() == bytes.size();
    }
    check(fit, "each row has the shape of its kind: a 5 byte call, a block whose tail fills the rest, a jump out with room for a branch");
    check(sameLength, "each written row replaces exactly the bytes it checks");
}

// The join point of each walk block: where the short jump in its tail lands, which is where the game's code goes on with the answer.
void theJoins() {
    struct Join {
        const char* id;
        uintptr_t address;
    };
    constexpr Join kJoins[] = {{"DWF4", 0x64E6E5}, {"DWP2", 0x657D75}, {"DWM1", 0x66D464}, {"SWD1", 0x5E9A87}, {"SWD2", 0x5E9CA9},
                               {"SWD3", 0x5EDAB3}, {"SWD4", 0x5EDAEF}, {"SWF1", 0x5DB21B}, {"SWF2", 0x5DCC48}, {"SWE1", 0x671394},
                               {"SWE2", 0x6719BE}, {"SWE3", 0x6722A4}};
    const std::vector<Site> sites = allSites();
    bool lands = true;
    for (const Join& join : kJoins) {
        const Site* site = siteNamed(sites, join.id);
        if (!site) {
            lands = false;
            continue;
        }
        const auto replacement = partner_replacement::replacementFor(*site, 0x10000000);
        const auto jump = std::find(replacement.begin() + kBranchBytes, replacement.end(), kShortJump);
        const uintptr_t at = static_cast<uintptr_t>(jump - replacement.begin());
        const bool ok = jump != replacement.end() && jump + 1 != replacement.end() &&
                        site->address + at + kShortJumpBytes + *(jump + 1) == join.address;
        if (!ok) std::printf("     %s does not jump to 0x%X\n", join.id, static_cast<unsigned>(join.address));
        lands = lands && ok;
    }
    check(lands, "the short jump of each block's tail lands on the join of section 4");
}

void theWrittenJumps() {
    const std::vector<Site> sites = allSites();
    struct Resume {
        const char* id;
        uintptr_t address;
    };
    constexpr Resume kResumes[] = {{"DWF1", sr2::kFraudStartBlockBody}, {"DWF2", sr2::kFraudStartBlockAfter}, {"DWF6", sr2::kResumeFraudRagdoll},
                                   {"DWP1", sr2::kResumeSnatchCleanup}, {"MB3", sr2::kMayhemBonusLoopAfter}};
    bool resumes = true;
    for (const Resume& resume : kResumes) {
        const Site* site = siteNamed(sites, resume.id);
        resumes = resumes && site && site->kind == Kind::JumpOut && site->address + sizeOf(*site) == resume.address;
    }
    check(resumes, "each jump out replaces the block that ends at its resume address");

    const Site* begin = siteNamed(sites, "DWF1");
    const auto beginBytes = partner_sites::parseHex(begin->expected);
    const uintptr_t beginEnd = begin->address + sizeOf(*begin);
    check(beginBytes[0] == 0x83 && beginBytes[1] == 0x7C && beginBytes[2] == 0x24 && beginBytes[3] == sr2::kFraudBlockHumanSlot &&
              beginBytes[4] == 0 && beginBytes[5] == 0x0F && beginBytes[6] == 0x84 && beginEnd + dwordOf(beginBytes, 7) == sr2::kFraudStartBlockExit,
          "DWF1 replaces cmp [esp+0x10], 0; je 0x64D359");

    const Site* loop = siteNamed(sites, "MB3");
    const auto loopBytes = partner_sites::parseHex(loop->expected);
    const size_t jneAt = loopBytes.size() - kNearJumpIfNotEqualBytes;
    check(loopBytes[jneAt] == 0x0F && loopBytes[jneAt + 1] == 0x85 &&
              loop->address + sizeOf(*loop) + dwordOf(loopBytes, jneAt + 2) == sr2::kMayhemBonusLoopTop,
          "MB3 replaces the loop's last jne back to 0x66BAF2");
}

void theCalls() {
    const std::vector<Site> sites = allSites();
    struct Callee {
        const char* id;
        uintptr_t function;
    };
    constexpr Callee kCallees[] = {
        {"DWF3", sr2::kHumanDamageMultiplier}, {"DWF5", sr2::kFreezeHuman},  {"DWP3", sr2::kRemoteStat},
        {"DWP4", sr2::kSendBroadcast},         {"DWK1", sr2::kDismissFollowers}, {"DWK2", sr2::kTraffickingArm},
    };
    bool callees = true;
    for (const Callee& callee : kCallees) {
        const Site* site = siteNamed(sites, callee.id);
        callees = callees && site && site->kind == Kind::RetargetCall && branchTarget(*site, 0) == callee.function;
    }
    check(callees, "the retargeted rows replace the call of the function they name");

    const Site* cleanup = siteNamed(sites, "DWP1");
    constexpr size_t kSetupCallAt = 4;  // push 1; mov ecx, esi; call 9DE310
    constexpr size_t kLookCallAt = 15;  // push 0; push esi; call 9E0350
    check(branchTarget(*cleanup, kSetupCallAt) == sr2::kSnatchSetupBoth && branchTarget(*cleanup, kLookCallAt) == sr2::kSnatchSetupLook,
          "DWP1 replaces the two calls of the Snatch cleanup");
    const Site* ragdoll = siteNamed(sites, "DWF6");
    constexpr size_t kRagdollCallAt = 62;  // push ebp; push eax; call 9ACC90
    check(branchTarget(*ragdoll, kRagdollCallAt) == sr2::kRagdollEnd, "DWF6 replaces the ragdoll end call");
}

// Everything the milestones before this one write, the diversions included.
std::vector<other_sites::Range> earlierWrites() {
    std::vector<other_sites::Range> others = other_sites::milestones3to8();
    const auto crowd = other_sites::players3and4();
    others.insert(others.end(), crowd.begin(), crowd.end());
    auto add = [&](const Site& site) { others.push_back({site.address, sizeOf(site)}); };
    for (const auto phase : {std::span<const Site>(activity_sites::kPhase1), std::span<const Site>(activity_sites::kPhase2),
                             std::span<const Site>(activity_sites::kPhase3)}) {
        for (const Site& site : phase) add(site);
    }
    for (const diversion_sites::Set& set : diversion_sites::kSets) {
        for (const Site& site : set.sites) add(site);
    }
    return others;
}

void theClearance() {
    const std::vector<Site> sites = allSites();
    std::set<std::string_view> ids;
    std::set<uintptr_t> addresses;
    bool unique = true;
    bool apart = true;
    for (const Site& a : sites) {
        unique = unique && ids.insert(a.id).second && addresses.insert(a.address).second;
        for (const Site& b : sites) apart = apart && (&a == &b || !other_sites::overlaps(a.address, sizeOf(a), b.address, sizeOf(b)));
    }
    check(unique, "ids and addresses are unique");
    check(apart, "no two rows overlap");
    const std::vector<other_sites::Range> others = earlierWrites();
    bool clear = true;
    bool clearOfJuiced = true;
    for (const Site& site : sites) {
        if (other_sites::anyOverlap(others, site.address, sizeOf(site))) {
            std::printf("     %s at 0x%X overlaps another milestone's write\n", site.id, static_cast<unsigned>(site.address));
            clear = false;
        }
        const uintptr_t first = site.address - kJuicedMargin;
        const uintptr_t last = site.address + sizeOf(site) + kJuicedMargin;
        if (std::any_of(std::begin(juiced::kTargets), std::end(juiced::kTargets), [&](uint32_t target) { return target >= first && target < last; })) {
            std::printf("     %s at 0x%X is within %u bytes of a Juiced target\n", site.id, static_cast<unsigned>(site.address),
                        static_cast<unsigned>(kJuicedMargin));
            clearOfJuiced = false;
        }
    }
    check(clear, "no row overlaps a write of milestones 3 to 16");
    check(clearOfJuiced, "no row within 16 bytes of an address Juiced touches");
}

}  // namespace

int main() {
    theCounts();
    theShapes();
    theJoins();
    theWrittenJumps();
    theCalls();
    theClearance();
    return g_failures == 0 ? 0 : 1;
}
