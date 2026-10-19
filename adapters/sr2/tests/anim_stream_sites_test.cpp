// The anim and asset streaming sites of milestone 16 W1 (no game): the bytes and shapes of section 3 of the research spec, unique and
// disjoint rows, clearance from every other milestone's write and from everything Juiced touches, what each pointer swap swaps, what
// each retargeted call replaces, and the 22 need table immediates against the relocation rule.
#include <algorithm>
#include <cstdio>
#include <set>
#include <string_view>
#include <vector>

#include "../src/anim_stream_rules.h"
#include "../src/anim_stream_sites.h"
#include "../src/partner_replacement.h"
#include "../src/vehicle_pool_references.h"
#include "../src/vehicle_pool_sites.h"
#include "juiced_targets.h"
#include "other_sites.h"

namespace {

using anim_stream_sites::NeedTableSite;
using partner_sites::Kind;
using partner_sites::Site;

constexpr uintptr_t kJuicedMargin = 16;
constexpr size_t kBranchBytes = 5;
constexpr size_t kPointerBytes = 4;
constexpr uintptr_t kThunk = 0x10004000;
constexpr uint32_t kMovedBase = 0x3A00000;
constexpr size_t kMovedRows = 14;

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_failures;
}

struct Range {
    const char* id;
    uintptr_t address;
    size_t size;
};

size_t sizeOf(const Site& site) { return partner_sites::parseHex(site.expected).size(); }

std::vector<Site> allSites() {
    std::vector<Site> all;
    for (const anim_stream_sites::Phase& phase : anim_stream_sites::kPhases) all.insert(all.end(), phase.sites.begin(), phase.sites.end());
    return all;
}

// Everything the phases touch, the need table immediates included.
std::vector<Range> allRanges() {
    std::vector<Range> ranges;
    for (const Site& site : allSites()) ranges.push_back({site.id, site.address, sizeOf(site)});
    for (const NeedTableSite& site : anim_stream_sites::kNeedTable) ranges.push_back({site.id, site.address, partner_sites::parseHex(site.expected).size()});
    return ranges;
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

// Where the `call rel32` of a retargeted call site goes.
uintptr_t callTarget(const Site& site) {
    return site.address + kBranchBytes + dwordOf(partner_sites::parseHex(site.expected), 1);
}

size_t writtenSites(const std::vector<Site>& sites) {
    return static_cast<size_t>(std::count_if(sites.begin(), sites.end(), [](const Site& site) { return site.kind != Kind::Bytes; }));
}

void thePhases() {
    using namespace anim_stream_sites;
    check(std::size(kPhases) == 6, "six phases: A to F");
    check(writtenCount(kPhases[0]) == 9 && kPhases[0].sites.size() == 11, "A anim sets per remote: 9 written sites (AS1 to AS5, AM1 to AM3, AL1) and the 2 checked rows AS0a and AL1a");
    check(writtenCount(kPhases[1]) == 23 && kPhases[1].sites.size() == 1 && kPhases[1].immediates.size() == 22, "B anim need table: AT0 and the 22 immediates");
    check(writtenCount(kPhases[2]) == 4, "C stream requests: 4 sites (SR1 to SR4)");
    check(writtenCount(kPhases[3]) == 2, "D set change gates: 2 sites (SC1, SC2)");
    check(writtenCount(kPhases[4]) == 4 && kPhases[4].sites.size() == 7, "E trigger sets: 4 written sites (TS1 to TS4) and the 3 checked rows TS3r, TS3d, TS2a");
    check(writtenCount(kPhases[5]) == 1, "F anim stream trace: 1 site (AN4)");
    size_t total = 0;
    for (const Phase& phase : kPhases) total += writtenCount(phase);
    check(total == 43, "43 written sites in all");
    bool onlyTheTraceWaits = true;
    for (size_t i = 0; i < std::size(kPhases); ++i) onlyTheTraceWaits = onlyTheTraceWaits && (kPhases[i].traceOnly == (i == 5));
    check(onlyTheTraceWaits, "only phase F is written with trace=1 only");
    check(writtenSites(allSites()) == 21, "21 written sites outside the need table immediates");
}

struct Expected {
    const char* id;
    uintptr_t address;
    const char* bytes;
};

// Section 3 of the spec, row by row.
constexpr Expected kSpec[] = {
    {"AS1", 0xE8BDCC, "30 3F A8 00"},           {"AS2", 0xA84702, "E8 39 00 D9 FF"},
    {"AS3", 0xA84145, "E8 F6 05 D9 FF"},        {"AS4", 0xA84261, "E8 DA 04 D9 FF"},
    {"AS5", 0xA849DC, "E8 5F FD D8 FF"},        {"AM1", 0xA82747, "C6 82 CF 00 00 00 01"},
    {"AM2", 0xA82750, "C6 85 CF 00 00 00 01"},  {"AM3", 0xA82686, "C6 86 CF 00 00 00 00"},
    {"AL1", 0x819943, "E8 28 A8 26 00"},        {"AS0a", 0x814670, "85 F6 53 8B 5C 24 08 55"},
    {"AL1a", 0xA82600, "66 81 FF FF FF 74 7E 33"}, {"AT0", 0xA83400, "81 EC CC 03 00 00 A1 50"},
    {"SR1", 0xE8BCC4, "10 92 A7 00"},           {"SR2", 0xE8BCDC, "60 93 A7 00"},
    {"SR3", 0xE8BCF4, "90 44 94 00"},           {"SR4", 0xE8BD0C, "D0 44 94 00"},
    {"SC1", 0xE8BCAC, "60 9D A7 00"},           {"SC2", 0xA78F10, "55 56 33 F6 57 B9 D0 B2"},
    {"TS1", 0xE8ABF0, "A0 85 68 00"},           {"TS2", 0x687B40, "83 EC 0C A1 50 0B E8 00"},
    {"TS3", 0x68875B, "83 C7 04 83 FF 08 7C 9D"}, {"TS3r", 0x688700, "8B 74 3C 08 85 F6 74 13"},
    {"TS3d", 0x688763, "6A 00 6A 04"},          {"TS4", 0x687A80, "83 EC 08 A1 50 0B E8 00"},
    {"TS2a", 0xA79240, "81 EC 20 05 00 00 A1 50"}, {"AN4", 0xA82EA0, "A0 48 8B 52 02 81 EC 1C"},
};

constexpr Expected kNeedSpec[] = {
    {"NT1", 0xA824E0, "B8 28 95 22 02"},     {"NT2", 0xA824F3, "3D A8 9B 22 02"},     {"NT3", 0xA82507, "BE 28 95 22 02"},
    {"NT4", 0xA8251F, "81 FE A8 9B 22 02"},  {"NT5", 0xA82609, "B8 F6 95 22 02"},     {"NT6", 0xA82622, "3D 76 9C 22 02"},
    {"NT7", 0xA82630, "81 C1 28 95 22 02"},  {"NT8", 0xA826A9, "B8 F4 95 22 02"},     {"NT9", 0xA826D7, "3D 74 9C 22 02"},
    {"NT10", 0xA826E9, "81 C5 28 95 22 02"}, {"NT11", 0xA8272E, "81 C2 28 95 22 02"}, {"NT12", 0xA82AE6, "B9 F4 95 22 02"},
    {"NT13", 0xA82AFE, "81 F9 74 9C 22 02"}, {"NT14", 0xA82B1A, "05 28 95 22 02"},     {"NT15", 0xA83E1C, "BE F7 95 22 02"},
    {"NT16", 0xA83E5A, "81 FE 77 9C 22 02"}, {"NT17", 0xA84172, "BE F6 95 22 02"},    {"NT18", 0xA841AC, "81 FE 76 9C 22 02"},
    {"NT19", 0xA84AB3, "BB F4 95 22 02"},    {"NT20", 0xA84B34, "81 FB 74 9C 22 02"}, {"NT21", 0xA84B3C, "BB F4 95 22 02"},
    {"NT22", 0xA84BB5, "81 FB 74 9C 22 02"},
};

void theBytes() {
    const std::vector<Site> sites = allSites();
    bool same = true;
    for (const Expected& row : kSpec) {
        const Site* site = siteNamed(sites, row.id);
        if (!site || site->address != row.address || site->expected != row.bytes) {
            std::printf("     %s differs from the spec\n", row.id);
            same = false;
        }
    }
    check(same && std::size(kSpec) == sites.size(), "every site has the address and the bytes of section 3, and none is missing");
    bool needSame = std::size(kNeedSpec) == std::size(anim_stream_sites::kNeedTable);
    for (size_t i = 0; needSame && i < std::size(kNeedSpec); ++i) {
        const NeedTableSite& site = anim_stream_sites::kNeedTable[i];
        needSame = std::string_view(site.id) == kNeedSpec[i].id && site.address == kNeedSpec[i].address && site.expected == kNeedSpec[i].bytes;
    }
    check(needSame, "the 22 need table rows have the addresses and the bytes of section 3");
}

void theShapes() {
    const std::vector<Site> sites = allSites();
    bool fit = true;
    bool sameLength = true;
    for (const Site& site : sites) {
        const auto bytes = partner_sites::parseHex(site.expected);
        if (site.kind == Kind::RetargetCall && (bytes.size() != kBranchBytes || bytes[0] != 0xE8)) fit = false;
        if (site.kind == Kind::HookEntry && bytes.size() < kBranchBytes) fit = false;
        if (site.kind == Kind::JumpOut && bytes.size() < kBranchBytes) fit = false;
        if (site.kind == Kind::CallBlock && bytes.size() != kBranchBytes + partner_sites::parseHex(site.tail).size()) fit = false;
        if (site.kind == Kind::PointerSwap && bytes.size() != kPointerBytes) fit = false;
        if (site.kind == Kind::Bytes && (!site.tail.empty() || site.thunk != partner_sites::Thunk::None)) fit = false;
        if (site.kind != Kind::HookEntry && site.kind != Kind::Bytes) {
            sameLength = sameLength && partner_replacement::replacementFor(site, kThunk).size() == bytes.size();
        }
    }
    check(fit, "each site has the shape of its kind: a 5 byte call, a hook with room for a branch, a call block whose tail fills the rest, a 4 byte pointer");
    check(sameLength, "each written site replaces exactly the bytes it checks");
    for (const char* id : {"AM1", "AM2", "AM3"}) {
        const Site* site = siteNamed(sites, id);
        check(site && site->kind == Kind::CallBlock && sizeOf(*site) == 7 && site->tail == "90 90", "AM1 to AM3: a 7 byte store becomes a call and 2 nops");
    }
    bool checkedWriteNothing = true;
    for (const Site& site : sites) {
        if (site.kind == Kind::Bytes) checkedWriteNothing = checkedWriteNothing && partner_replacement::replacementFor(site, 0).empty();
    }
    check(checkedWriteNothing, "a checked row writes nothing");
}

void theUniqueness() {
    const std::vector<Range> ranges = allRanges();
    std::set<std::string_view> ids;
    bool unique = true;
    bool apart = true;
    for (const Range& a : ranges) {
        unique = unique && ids.insert(a.id).second;
        for (const Range& b : ranges) {
            if (&a != &b && other_sites::overlaps(a.address, a.size, b.address, b.size)) apart = false;
        }
    }
    check(unique, "every id is used once");
    check(apart, "no two rows overlap");
    std::set<partner_sites::Thunk> thunks;
    bool oneSiteEach = true;
    for (const Site& site : allSites()) {
        if (site.thunk == partner_sites::Thunk::None || site.thunk == partner_sites::Thunk::SendStreamingSync) continue;
        oneSiteEach = oneSiteEach && thunks.insert(site.thunk).second;
    }
    check(oneSiteEach, "each thunk but the shared send belongs to one site");
}

std::vector<other_sites::Range> everyEarlierWrite() {
    std::vector<other_sites::Range> others = other_sites::milestones3to8();
    const auto activities = other_sites::activities();
    const auto crowd = other_sites::players3and4();
    others.insert(others.end(), activities.begin(), activities.end());
    others.insert(others.end(), crowd.begin(), crowd.end());
    for (const auto table : multiplayer_sites::kScriptTraces) {
        for (const Site& site : table) others.push_back({site.address, sizeOf(site)});
    }
    for (const auto& site : player_sites::kSkinPool) others.push_back({site.address, partner_sites::parseHex(site.expected).size()});
    for (const auto& reference : vehicle_pool_references::kReferences) others.push_back({reference.site, kPointerBytes});
    for (const auto& bound : vehicle_pool_sites::kBounds) others.push_back({bound.address, partner_sites::parseHex(bound.expected).size()});
    for (const Site& site : vehicle_pool_sites::kHeadroomBlocks) others.push_back({site.address, sizeOf(site)});
    return others;
}

void theClearance() {
    const std::vector<other_sites::Range> others = everyEarlierWrite();
    bool clear = true;
    bool clearOfJuiced = true;
    for (const Range& range : allRanges()) {
        if (other_sites::anyOverlap(others, range.address, range.size)) {
            std::printf("     %s at 0x%X overlaps a site of an earlier milestone\n", range.id, static_cast<unsigned>(range.address));
            clear = false;
        }
        const uintptr_t first = range.address - kJuicedMargin;
        const uintptr_t last = range.address + range.size + kJuicedMargin;
        if (std::any_of(std::begin(juiced::kTargets), std::end(juiced::kTargets), [&](uint32_t target) { return target >= first && target < last; })) {
            std::printf("     %s at 0x%X is within %u bytes of a Juiced target\n", range.id, static_cast<unsigned>(range.address), static_cast<unsigned>(kJuicedMargin));
            clearOfJuiced = false;
        }
    }
    check(clear, "no row overlaps a write of milestones 3 to 15 (the partner, activity, players 3 and 4, skin pool and vehicle pool tables)");
    check(clearOfJuiced, "no row within 16 bytes of an address Juiced touches");
}

void theTargets() {
    const std::vector<Site> sites = allSites();
    struct Swap {
        const char* id;
        uintptr_t slot;
        uintptr_t handler;
    };
    constexpr Swap kSwaps[] = {
        {"AS1", sr2::kStreamingSyncHandlerSlot, sr2::kStreamingSyncHandler},
        {"SR1", sr2::kStrmReqSlot, 0xA79210},
        {"SR2", sr2::kStrmRemReqSlot, 0xA79360},
        {"SR3", sr2::kCharReqSlot, 0x944490},
        {"SR4", sr2::kCharRemReqSlot, 0x9444D0},
        {"SC1", sr2::kSetChangeSlot, 0xA79D60},
        {"TS1", sr2::kMiscTriggerSlot, 0x6885A0},
    };
    bool decoded = true;
    for (const Swap& swap : kSwaps) {
        const Site* site = siteNamed(sites, swap.id);
        decoded = decoded && site && site->kind == Kind::PointerSwap && site->address == swap.slot &&
                  dwordOf(partner_sites::parseHex(site->expected), 0) == swap.handler &&
                  partner_replacement::replacementFor(*site, kThunk) == std::vector<uint8_t>{0x00, 0x40, 0x00, 0x10};
    }
    check(decoded, "each pointer swap holds the handler of its message and writes the thunk's address little endian");
    bool sends = true;
    for (const char* id : {"AS2", "AS3", "AS4", "AS5"}) {
        const Site* site = siteNamed(sites, id);
        sends = sends && site && site->kind == Kind::RetargetCall && callTarget(*site) == sr2::kSendBroadcast && site->thunk == partner_sites::Thunk::SendStreamingSync;
    }
    check(sends, "AS2 to AS5 each replace a call of the broadcast 0x814740 and share one send thunk");
    const Site* leaver = siteNamed(sites, "AL1");
    check(leaver && leaver->address == sr2::kMemberLeftNeedClear && callTarget(*leaver) == sr2::kClearEveryRemoteNeed, "AL1 replaces the call that clears every remote need (0xA84170)");
    const Site* hook = siteNamed(sites, "AT0");
    check(hook && hook->kind == Kind::HookEntry && hook->address == sr2::kAnimNeedInit, "AT0 hooks the need table reset");
    const Site* load = siteNamed(sites, "AN4");
    check(load && load->kind == Kind::HookEntry && load->address == sr2::kAnimSetLoad, "AN4 hooks the anim set load");
    check(siteNamed(sites, "TS2")->address == sr2::kTriggerTick && siteNamed(sites, "TS4")->address == sr2::kTriggerReset &&
              siteNamed(sites, "TS3r")->address == sr2::kTriggerCleanupLoop && siteNamed(sites, "TS3d")->address == sr2::kTriggerCleanupDone &&
              siteNamed(sites, "TS2a")->address == sr2::kStreamRelease && siteNamed(sites, "SC2")->address == sr2::kStreamReset &&
              siteNamed(sites, "AS0a")->address == sr2::kSendToMember && siteNamed(sites, "AL1a")->address == sr2::kAnimNeedClear,
          "the trigger, reset, release, send and need clear sites are the addresses the constants name");
}

void theNeedTable() {
    using anim_stream_rules::relocatedNeedRef;
    constexpr uint32_t kFields[] = {0, 0xCC, 0xCE, 0xCF};
    check(sr2::kAnimNeedTableEnd == sr2::kAnimNeedTable + sr2::kAnimNeedRows * sr2::kAnimNeedRowBytes, "the shipped end is the base plus 8 rows of 0xD0");
    bool classified = true;
    bool moved = true;
    bool shippedUnchanged = true;
    bool inInstruction = true;
    for (const NeedTableSite& site : anim_stream_sites::kNeedTable) {
        const uint32_t value = anim_stream_sites::immediateOf(site);
        const bool end = value >= sr2::kAnimNeedTableEnd;
        const uint32_t origin = end ? static_cast<uint32_t>(sr2::kAnimNeedTableEnd) : static_cast<uint32_t>(sr2::kAnimNeedTable);
        classified = classified && std::count(std::begin(kFields), std::end(kFields), value - origin) == 1;
        const uint32_t expected = end ? kMovedBase + kMovedRows * sr2::kAnimNeedRowBytes + (value - origin) : kMovedBase + (value - origin);
        moved = moved && relocatedNeedRef(value, kMovedBase, kMovedRows) == expected;
        shippedUnchanged = shippedUnchanged && relocatedNeedRef(value, sr2::kAnimNeedTable, sr2::kAnimNeedRows) == value;
        inInstruction = inInstruction && site.immediateOffset + sizeof(uint32_t) == partner_sites::parseHex(site.expected).size();
    }
    check(classified, "each of the 22 dwords is the base or the end plus 0, 0xCC, 0xCE or 0xCF");
    check(moved, "each moves with the table: a base to the new base, an end to the new end, with its field offset");
    check(shippedUnchanged, "at the shipped base and 8 rows none changes");
    check(inInstruction, "each dword is the last four bytes of its instruction");
    size_t bases = 0;
    size_t ends = 0;
    for (const NeedTableSite& site : anim_stream_sites::kNeedTable) (anim_stream_sites::immediateOf(site) >= sr2::kAnimNeedTableEnd ? ends : bases)++;
    check(bases == 13 && ends == 9, "13 references to the base and 9 to the end");
    check(anim_stream_sites::kNeedTableBaseImmediate == 0xA824E1 && anim_stream_sites::kNeedTableEndImmediate == 0xA824F4,
          "the code's own base and end (NT1 and NT2) are where the trace reads the table");
}

}  // namespace

int main() {
    thePhases();
    theBytes();
    theShapes();
    theUniqueness();
    theClearance();
    theTargets();
    theNeedTable();
    return g_failures == 0 ? 0 : 1;
}
