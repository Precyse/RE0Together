// The site table of one character type per remote (M16 W1, no game): eight rows with unique ids, their shapes, the resume addresses
// against the check rows, and clearance from Juiced and from every other site.
#include <algorithm>
#include <cstdio>
#include <set>
#include <string_view>
#include <vector>

#include "../src/char_types_sites.h"
#include "../src/partner_sites.h"
#include "../src/player_sites.h"
#include "check.h"
#include "juiced_targets.h"
#include "other_sites.h"

namespace {

using char_types_sites::Kind;
using char_types_sites::Row;
using char_types_sites::kRows;

constexpr uintptr_t kJuicedMargin = 16;
constexpr size_t kBranchBytes = 5;
constexpr size_t kExpectedRows = 8;

size_t sizeOf(const Row& row) { return partner_sites::parseHex(row.expected).size(); }

const Row* rowNamed(std::string_view id) {
    for (const Row& row : kRows) {
        if (id == row.id) return &row;
    }
    return nullptr;
}

bool idsAreUnique() {
    std::set<std::string_view> ids;
    for (const Row& row : kRows) {
        if (!ids.insert(row.id).second) return false;
    }
    return true;
}

bool expectedBytesParse() {
    for (const Row& row : kRows) {
        const size_t size = sizeOf(row);
        if (size == 0 || row.expected.size() != size * 3 - 1) return false;
    }
    return true;
}

bool rowsDoNotOverlap() {
    for (const Row& a : kRows) {
        for (const Row& b : kRows) {
            if (&a != &b && other_sites::overlaps(a.address, sizeOf(a), b.address, sizeOf(b))) return false;
        }
    }
    return true;
}

bool clearOfJuiced() {
    for (const Row& row : kRows) {
        const uintptr_t first = row.address - kJuicedMargin;
        const uintptr_t last = row.address + sizeOf(row) + kJuicedMargin;
        if (std::any_of(std::begin(juiced::kTargets), std::end(juiced::kTargets), [&](uint32_t target) { return target >= first && target < last; })) {
            std::printf("     %s at 0x%X is within %u bytes of a Juiced target\n", row.id, static_cast<unsigned>(row.address), static_cast<unsigned>(kJuicedMargin));
            return false;
        }
    }
    return true;
}

std::vector<other_sites::Range> earlierSites() {
    std::vector<other_sites::Range> ranges = other_sites::milestones3to8();
    for (const std::vector<other_sites::Range>& more : {other_sites::activities(), other_sites::players3and4()}) ranges.insert(ranges.end(), more.begin(), more.end());
    for (const player_sites::SkinPoolSite& site : player_sites::kSkinPool) ranges.push_back({site.address, partner_sites::parseHex(site.expected).size()});
    for (const partner_sites::Site& site : player_sites::kCutsceneVisuals) ranges.push_back({site.address, partner_sites::parseHex(site.expected).size()});
    return ranges;
}

bool clearOfEarlierSites() {
    const std::vector<other_sites::Range> others = earlierSites();
    for (const Row& row : kRows) {
        if (other_sites::anyOverlap(others, row.address, sizeOf(row))) {
            std::printf("     %s at 0x%X overlaps an earlier site\n", row.id, static_cast<unsigned>(row.address));
            return false;
        }
    }
    return true;
}

bool resumeMatchesItsCheckRow(std::string_view jumpOut, std::string_view check) {
    const Row* from = rowNamed(jumpOut);
    const Row* to = rowNamed(check);
    return from && to && to->kind == Kind::Check && from->kind == Kind::JumpOut && from->resume == to->address;
}

void theRows() {
    check(char_types_sites::kRowCount == kExpectedRows, "8 rows");
    check(idsAreUnique(), "the ids are unique");
    check(expectedBytesParse(), "every expected string is hex bytes separated by single spaces");
    check(rowsDoNotOverlap(), "no row overlaps another");
}

void theShapes() {
    const Row* ct1 = rowNamed("CT1");
    const auto prologue = ct1 ? partner_sites::parseHex(ct1->expected) : std::vector<uint8_t>{};
    check(ct1 && ct1->kind == Kind::HookEntry && prologue.size() >= kBranchBytes && prologue[0] == 0xA0 && prologue[1] == 0x49 &&
              prologue[2] == 0x8B && prologue[3] == 0x52 && prologue[4] == 0x02,
          "CT1 is a hook entry that starts with mov al, [0x2528B49] (A0 49 8B 52 02)");
    for (const char* id : {"CT2", "CT3"}) {
        const Row* row = rowNamed(id);
        check(row && row->kind == Kind::JumpOut && sizeOf(*row) >= kBranchBytes, "CT2 and CT3 are jump outs of at least 5 bytes");
    }
    bool onlyJumpOutsResume = true;
    for (const Row& row : kRows) onlyJumpOutsResume = onlyJumpOutsResume && (row.kind == Kind::JumpOut) == (row.resume != 0);
    check(onlyJumpOutsResume, "only the jump outs have a resume address");
    check(resumeMatchesItsCheckRow("CT2", "CT2r"), "CT2 resumes at the address of CT2r");
    check(resumeMatchesItsCheckRow("CT3", "CT3r"), "CT3 resumes at the address of CT3r");
    check(rowNamed("CT2") && rowNamed("CT2")->address + sizeOf(*rowNamed("CT2")) == rowNamed("CT2")->resume,
          "CT2 resumes right after the displaced instruction");
    for (const char* id : {"CT1g", "CT2b", "CT3s"}) check(rowNamed(id) && rowNamed(id)->kind == Kind::Check, "CT1g, CT2b and CT3s are check only");
}

void theClearance() {
    check(clearOfJuiced(), "every row is more than 16 bytes from every Juiced target");
    check(clearOfEarlierSites(), "no row overlaps a site of milestones 3 to 9, the activities, the skin pool or the cutscene visuals");
}

}  // namespace

int main() {
    theRows();
    theShapes();
    theClearance();
    return g_failures == 0 ? 0 : 1;
}
