// The milestone 17 wave 3 site tables (no game): the row count of every set, the shapes of each kind, where the calls and the
// resume addresses of the written bytes land, and clearance from the earlier milestones' writes and from everything Juiced touches.
#include <algorithm>
#include <cstdio>
#include <iterator>
#include <set>
#include <span>
#include <string_view>
#include <vector>

#include "../src/activity_sites.h"
#include "../src/crowd_control_sites.h"
#include "../src/diversion_sites.h"
#include "../src/partner_replacement.h"
#include "check.h"
#include "juiced_targets.h"
#include "other_sites.h"
#include "site_lookup.h"

namespace {

using partner_sites::Kind;
using partner_sites::Site;

constexpr uintptr_t kClearance = 16;
constexpr size_t kBranchBytes = 5;

std::vector<Site> allSites() {
    std::vector<Site> all;
    for (const crowd_control_sites::Set& set : crowd_control_sites::kSets) all.insert(all.end(), set.sites.begin(), set.sites.end());
    return all;
}

void theCounts() {
    struct Count {
        const char* set;
        size_t rows;
    };
    constexpr Count kCounts[] = {{"hold", 6}, {"phases", 3}, {"start", 4}, {"end", 3}};
    bool same = std::size(kCounts) == std::size(crowd_control_sites::kSets);
    for (size_t i = 0; same && i < std::size(kCounts); ++i) {
        same = std::string_view(crowd_control_sites::kSets[i].name) == kCounts[i].set && crowd_control_sites::kSets[i].sites.size() == kCounts[i].rows;
    }
    check(same, "the sets: hold 6, phases 3, start 4, end 3");
    check(allSites().size() == 16, "16 rows");
}

void theShapes() {
    bool fit = true;
    bool sameLength = true;
    for (const Site& site : allSites()) {
        const auto bytes = partner_sites::parseHex(site.expected);
        if (site.kind == Kind::RetargetCall && (bytes.size() != kBranchBytes || bytes[0] != 0xE8)) fit = false;
        if (site.kind == Kind::JumpOut && bytes.size() < kBranchBytes) fit = false;
        if (site.kind != Kind::RetargetCall && site.kind != Kind::JumpOut) fit = false;
        if (!site.tail.empty()) fit = false;
        sameLength = sameLength && partner_replacement::replacementFor(site, 0x10000000).size() == bytes.size();
    }
    check(fit, "each row is a 5 byte call that starts with E8 or a jump out with room for a branch, and none has a tail");
    check(sameLength, "each written row replaces exactly the bytes it checks");
}

void theCalls() {
    const std::vector<Site> sites = allSites();
    bool getters = true;
    for (const char* id : {"CR1", "CR2", "CR3", "CR4", "CR5", "CR6", "CR7", "CR8"}) {
        const Site* site = siteNamed(sites, id);
        getters = getters && site && site->kind == Kind::RetargetCall && callTarget(*site) == sr2::kRemotePlayerGetter;
    }
    check(getters, "CR1 to CR8 retarget a call of the partner getter 0x9D7BD0");
    const Site* dismiss = siteNamed(sites, "CR12");
    check(dismiss && callTarget(*dismiss) == sr2::kDismissFollowers && dismiss->thunk == partner_sites::Thunk::EachRemoteDismissFollowers,
          "CR12 retargets the call of 0x98F460 to the existing dismiss thunk of the diversions");
    bool warps = true;
    for (const char* id : {"CR13", "CR16"}) {
        const Site* site = siteNamed(sites, id);
        warps = warps && site && callTarget(*site) == sr2::kTeleportHuman && site->thunk == partner_sites::Thunk::TeleportLocalOrEachRemote;
    }
    check(warps, "CR13 and CR16 retarget a call of the teleport 0x9DB480");
}

void theResumes() {
    const std::vector<Site> sites = allSites();
    struct Resume {
        const char* id;
        size_t length;
        uintptr_t address;   // the spec's resume address
        uintptr_t constant;  // the one the thunk jumps to
    };
    constexpr Resume kResumes[] = {{"CR9", 37, 0x5A1B55, sr2::kResumeProtectStart},      {"CR10", 112, 0x5AD8E0, sr2::kResumeCrowdWeapon},
                                   {"CR11", 178, 0x5AD9C4, sr2::kResumeCrowdStartFlags}, {"CR14", 25, 0x5ADF19, sr2::kResumeCrowdRelease},
                                   {"CR15", 168, 0x5AE224, sr2::kResumeCrowdEndFlags}};
    bool resumes = true;
    bool constants = true;
    for (const Resume& resume : kResumes) {
        const Site* site = siteNamed(sites, resume.id);
        resumes = resumes && site && site->kind == Kind::JumpOut && sizeOf(*site) == resume.length && site->address + resume.length == resume.address;
        constants = constants && resume.constant == resume.address;
    }
    check(resumes, "each jump out has the length of section 4 and ends at its resume address");
    check(constants, "the resume addresses of sr2_addresses.h are those the jump outs end at");
}

// Everything the earlier milestones write that this one has to stay clear of.
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
        if (other_sites::anyOverlap(others, site.address - kClearance, sizeOf(site) + 2 * kClearance)) {
            std::printf("     %s at 0x%X is within %u bytes of another milestone's write\n", site.id, static_cast<unsigned>(site.address),
                        static_cast<unsigned>(kClearance));
            clear = false;
        }
        const uintptr_t first = site.address - kClearance;
        const uintptr_t last = site.address + sizeOf(site) + kClearance;
        if (std::any_of(std::begin(juiced::kTargets), std::end(juiced::kTargets), [&](uint32_t target) { return target >= first && target < last; })) {
            std::printf("     %s at 0x%X is within %u bytes of an address Juiced touches\n", site.id, static_cast<unsigned>(site.address),
                        static_cast<unsigned>(kClearance));
            clearOfJuiced = false;
        }
    }
    check(clear, "no row within 16 bytes of a write of an earlier milestone");
    check(clearOfJuiced, "no row within 16 bytes of an address Juiced touches");
}

}  // namespace

int main() {
    theCounts();
    theShapes();
    theCalls();
    theResumes();
    theClearance();
    return g_failures == 0 ? 0 : 1;
}
