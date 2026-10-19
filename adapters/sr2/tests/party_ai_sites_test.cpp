// The milestone 16 wave 2 site tables (no game): row counts per phase, ids and addresses, shapes, the callee of each retargeted call,
// the two call blocks and where their tails land, clearance from every other site and from everything Juiced touches, and a thunk for
// every written site.
#include <algorithm>
#include <cstdio>
#include <set>
#include <span>
#include <string_view>
#include <vector>

#include "../src/anim_stream_sites.h"
#include "../src/char_types_sites.h"
#include "../src/multiplayer_sites.h"
#include "../src/outfit_sync_sites.h"
#include "../src/party_ai_sites.h"
#include "../src/party_ai_thunks.h"
#include "../src/partner_replacement.h"
#include "../src/session_kick_sites.h"
#include "../src/sr2_addresses.h"
#include "../src/vehicle_pool_references.h"
#include "../src/vehicle_pool_sites.h"
#include "juiced_targets.h"
#include "other_sites.h"

namespace logger {
void write(const char*, ...) {}
void writeUnlessRepeated(const char*, ...) {}
}  // namespace logger

namespace debug_stats {
void setError(const char*, ...) {}
}  // namespace debug_stats

namespace {

using partner_sites::Kind;
using partner_sites::Site;

constexpr uintptr_t kClearance = 16;
constexpr size_t kBranchBytes = 5;
constexpr size_t kHookBytes = 8;
constexpr size_t kDwordBytes = 4;
constexpr size_t kShortJumpBytes = 2;
constexpr uint8_t kCallOpcode = 0xE8;
constexpr uint8_t kShortJumpOpcode = 0xEB;
constexpr uintptr_t kThunk = 0x10004000;

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_failures;
}

std::vector<Site> allSites() {
    std::vector<Site> all;
    for (const party_ai_sites::Phase& phase : party_ai_sites::kPhases) all.insert(all.end(), phase.sites.begin(), phase.sites.end());
    return all;
}

std::vector<Site> writtenSites() {
    std::vector<Site> written;
    for (const Site& site : allSites()) {
        if (site.kind != Kind::Bytes) written.push_back(site);
    }
    return written;
}

size_t sizeOf(const Site& site) { return partner_sites::parseHex(site.expected).size(); }

const Site* siteNamed(const std::vector<Site>& sites, std::string_view id) {
    for (const Site& site : sites) {
        if (id == site.id) return &site;
    }
    return nullptr;
}

// Where the `call rel32` of a retargeted call site goes.
uintptr_t callTarget(const Site& site) {
    const auto bytes = partner_sites::parseHex(site.expected);
    const uint32_t displacement = bytes[1] | bytes[2] << 8 | bytes[3] << 16 | static_cast<uint32_t>(bytes[4]) << 24;
    return site.address + kBranchBytes + displacement;
}

// Where the `jmp rel8` that a call block's tail starts with lands.
uintptr_t tailTarget(const Site& site) {
    const auto tail = partner_sites::parseHex(site.tail);
    const auto bytes = partner_replacement::replacementFor(site, kThunk);
    if (tail.size() != kShortJumpBytes || tail[0] != kShortJumpOpcode || bytes.size() != kBranchBytes + kShortJumpBytes) return 0;
    return site.address + kBranchBytes + kShortJumpBytes + static_cast<int8_t>(tail[1]);
}

std::set<std::string_view> otherIds() {
    std::set<std::string_view> ids;
    for (const Site& site : partner_sites::kSites) ids.insert(site.id);
    for (const multiplayer_sites::Phase& phase : multiplayer_sites::kPhases) {
        for (const Site& site : phase.sites) ids.insert(site.id);
    }
    for (const auto table : multiplayer_sites::kScriptTraces) {
        for (const Site& site : table) ids.insert(site.id);
    }
    for (const anim_stream_sites::Phase& phase : anim_stream_sites::kPhases) {
        for (const Site& site : phase.sites) ids.insert(site.id);
    }
    for (const session_kick_sites::Set& set : session_kick_sites::kSets) {
        for (const Site& site : set.sites) ids.insert(site.id);
    }
    return ids;
}

void theTables() {
    const std::vector<Site> all = allSites();
    const auto& phases = party_ai_sites::kPhases;
    const auto writtenIn = [](const party_ai_sites::Phase& phase) {
        return static_cast<size_t>(std::count_if(phase.sites.begin(), phase.sites.end(), [](const Site& site) { return site.kind != Kind::Bytes; }));
    };
    check(std::size(phases) == 5, "five phases");
    check(writtenIn(phases[0]) == 3 && phases[0].sites.size() == 8, "party: 3 written rows (PA1 to PA3) and 5 checks");
    check(writtenIn(phases[1]) == 1 && phases[1].sites.size() == 2, "party trace: 1 written row (PT1) and 1 check");
    check(writtenIn(phases[2]) == 7 && phases[2].sites.size() == 10, "npc sight and range: 7 written rows (AI1 to AI7) and 3 checks");
    check(writtenIn(phases[3]) == 4 && phases[3].sites.size() == 5, "npc team: 4 written rows (AI8 to AI11) and 1 check");
    check(writtenIn(phases[4]) == 1 && phases[4].sites.size() == 7, "team odds: 1 written row (AI12) and 6 checks");
    check(writtenSites().size() == 16, "16 written rows");
    check(all.size() == 32, "32 rows in all");
    bool traceOnlyIsPartyTrace = true;
    for (const party_ai_sites::Phase& phase : phases) traceOnlyIsPartyTrace = traceOnlyIsPartyTrace && phase.traceOnly == (phase.sites.data() == party_ai_sites::kPartyTrace);
    check(traceOnlyIsPartyTrace, "only the party trace phase waits for trace=1");

    std::set<std::string_view> ids;
    std::set<uintptr_t> writtenAddresses;
    const std::set<std::string_view> others = otherIds();
    bool unique = true;
    for (const Site& site : all) {
        unique = unique && ids.insert(site.id).second && others.count(site.id) == 0;
        if (site.kind != Kind::Bytes) unique = unique && writtenAddresses.insert(site.address).second;
    }
    check(unique, "ids are unique and unlike any other milestone's, written addresses are unique");
    check(std::all_of(all.begin(), all.end(), [](const Site& site) { return (site.kind == Kind::Bytes) == (site.thunk == partner_sites::Thunk::None); }),
          "a check row has no thunk and every other row has one");
    check(std::all_of(all.begin(), all.end(), [](const Site& site) { return site.kind != Kind::Bytes || site.tail.empty(); }), "a check row writes nothing");
}

void theShapes() {
    const std::vector<Site> written = writtenSites();
    bool calls = true;
    bool hooks = true;
    for (const Site& site : written) {
        const auto bytes = partner_sites::parseHex(site.expected);
        if (site.kind == Kind::RetargetCall) calls = calls && bytes.size() == kBranchBytes && bytes[0] == kCallOpcode;
        if (site.kind == Kind::HookEntry) hooks = hooks && bytes.size() == kHookBytes;
    }
    check(calls, "every retargeted call is 5 bytes starting with E8");
    check(hooks, "every hook checks 8 bytes");
    check(std::count_if(written.begin(), written.end(), [](const Site& site) { return site.kind == Kind::RetargetCall; }) == 12 &&
              std::count_if(written.begin(), written.end(), [](const Site& site) { return site.kind == Kind::HookEntry; }) == 2 &&
              std::count_if(written.begin(), written.end(), [](const Site& site) { return site.kind == Kind::CallBlock; }) == 2,
          "12 retargeted calls, 2 hooks (PT1, AI12) and 2 call blocks (PA2, PA3)");

    const Site* derby = siteNamed(written, "PA2");
    const Site* septic = siteNamed(written, "PA3");
    const auto derbyBytes = partner_replacement::replacementFor(*derby, kThunk);
    const auto septicBytes = partner_replacement::replacementFor(*septic, kThunk);
    check(derbyBytes.size() == kBranchBytes + kShortJumpBytes && derbyBytes[0] == kCallOpcode && derbyBytes[kBranchBytes] == kShortJumpOpcode &&
              septicBytes.size() == kBranchBytes + kShortJumpBytes && septicBytes[0] == kCallOpcode && septicBytes[kBranchBytes] == kShortJumpOpcode,
          "PA2 and PA3 are a call to the thunk and a short jump");
    check(derbyBytes.size() <= sizeOf(*derby) && septicBytes.size() <= sizeOf(*septic), "the written bytes fit in the block each replaces");
    check(tailTarget(*derby) == sr2::kDerbyEnterResume && tailTarget(*septic) == sr2::kSepticEnterResume,
          "the tail of PA2 lands on 0x53B744 and the tail of PA3 on 0x53B68E");
}

void theCallees() {
    const std::vector<Site> written = writtenSites();
    const auto callee = [&](std::string_view id) { return callTarget(*siteNamed(written, id)); };
    bool getter = true;
    for (const char* id : {"PA1", "AI3", "AI4", "AI5", "AI8", "AI9", "AI10", "AI11"}) getter = getter && callee(id) == sr2::kRemotePlayerGetter;
    check(getter, "PA1, AI3, AI4, AI5, AI8, AI9, AI10 and AI11 each call the partner getter 0x9D7BD0");
    check(callee("AI1") == sr2::kRayBlocked && callee("AI2") == sr2::kRayBlocked, "AI1 and AI2 call the ray test 0x4294C0");
    check(callee("AI6") == sr2::kInView, "AI6 calls the facing test 0x451000");
    check(callee("AI7") == sr2::kSquaredDistance, "AI7 calls the squared distance 0xBDEA30");
}

std::vector<other_sites::Range> otherRanges() {
    std::vector<other_sites::Range> others = other_sites::milestones3to8();
    for (const auto& more : {other_sites::activities(), other_sites::players3and4()}) others.insert(others.end(), more.begin(), more.end());
    const auto addSites = [&](std::span<const Site> sites) {
        for (const Site& site : sites) others.push_back({site.address, sizeOf(site)});
    };
    for (const auto table : multiplayer_sites::kScriptTraces) addSites(table);
    addSites(outfit_sync_sites::kLookSync);
    addSites(outfit_sync_sites::kItemWear);
    addSites(outfit_sync_sites::kWeaponSlots);
    addSites(outfit_sync_sites::kTraces);
    addSites(vehicle_pool_sites::kHeadroomBlocks);
    for (const session_kick_sites::Set& set : session_kick_sites::kSets) addSites(set.sites);
    for (const anim_stream_sites::Phase& phase : anim_stream_sites::kPhases) {
        addSites(phase.sites);
        for (const anim_stream_sites::NeedTableSite& site : phase.immediates) others.push_back({site.address, partner_sites::parseHex(site.expected).size()});
    }
    for (const auto& row : char_types_sites::kRows) others.push_back({row.address, partner_sites::parseHex(row.expected).size()});
    for (const auto& row : vehicle_pool_references::kReferences) others.push_back({row.site, kDwordBytes});
    for (const auto& bound : vehicle_pool_sites::kBounds) others.push_back({bound.address, partner_sites::parseHex(bound.expected).size()});
    return others;
}

void theClearance() {
    const std::vector<Site> written = writtenSites();
    bool apart = true;
    for (const Site& a : written) {
        for (const Site& b : written) {
            if (&a != &b && other_sites::overlaps(a.address, sizeOf(a), b.address, sizeOf(b))) apart = false;
        }
    }
    check(apart, "no two written rows overlap");

    const std::vector<other_sites::Range> others = otherRanges();
    bool clear = true;
    bool clearOfJuiced = true;
    for (const Site& site : written) {
        const uintptr_t first = site.address - kClearance;
        const uintptr_t last = site.address + sizeOf(site) + kClearance;
        if (other_sites::anyOverlap(others, first, last - first)) {
            std::printf("     %s at 0x%X is within %u bytes of a site of another milestone\n", site.id, static_cast<unsigned>(site.address),
                        static_cast<unsigned>(kClearance));
            clear = false;
        }
        if (std::any_of(std::begin(juiced::kTargets), std::end(juiced::kTargets), [&](uint32_t target) { return target >= first && target < last; })) {
            std::printf("     %s at 0x%X is within %u bytes of a Juiced target\n", site.id, static_cast<unsigned>(site.address),
                        static_cast<unsigned>(kClearance));
            clearOfJuiced = false;
        }
    }
    check(clear, "every written row is 16 bytes clear of the sites of the other milestones");
    check(clearOfJuiced, "every written row is 16 bytes clear of a Juiced target");
}

void theBindings() {
    bool allBound = true;
    bool hooksHaveSlots = true;
    bool slotsAreDistinct = true;
    std::set<void**> slots;
    for (const Site& site : writtenSites()) {
        const partner_thunks::Binding binding = party_ai_thunks::bind(site.thunk);
        if (!binding.entry) {
            std::printf("     %s has no thunk\n", site.id);
            allBound = false;
            continue;
        }
        if (site.kind != Kind::HookEntry) continue;
        hooksHaveSlots = hooksHaveSlots && binding.original;
        slotsAreDistinct = slotsAreDistinct && slots.insert(binding.original).second;
    }
    check(allBound, "every written row has a thunk");
    check(hooksHaveSlots, "every hook has a slot for its trampoline");
    check(slotsAreDistinct, "no two hooks share a trampoline slot");
    check(party_ai_thunks::bind(partner_sites::Thunk::None).entry == nullptr, "the Bytes thunk has no code");
}

}  // namespace

int main() {
    theTables();
    theShapes();
    theCallees();
    theClearance();
    theBindings();
    return g_failures == 0 ? 0 : 1;
}
