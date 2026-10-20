// The milestone 16 wave 2 site tables (no game): row counts and ids per set, the bytes and shapes of each row, what each retargeted call
// replaces, the replacements of the two multi-byte blocks, the resume addresses against their check rows, and clearance from every
// other milestone's write and from everything Juiced touches.
#include <algorithm>
#include <cstdio>
#include <set>
#include <span>
#include <string_view>
#include <vector>

#include "../src/anim_stream_sites.h"
#include "../src/net_handlers_sites.h"
#include "../src/outfit_sync_sites.h"
#include "../src/partner_replacement.h"
#include "../src/player_sites.h"
#include "../src/session_kick_sites.h"
#include "../src/sr2_addresses.h"
#include "../src/trace_sites.h"
#include "../src/vehicle_pool_references.h"
#include "../src/vehicle_pool_sites.h"
#include "check.h"
#include "juiced_targets.h"
#include "other_sites.h"
#include "site_lookup.h"

namespace {

using partner_sites::Kind;
using partner_sites::Site;
using partner_sites::Thunk;

constexpr uintptr_t kClearance = 16;
constexpr size_t kBranchBytes = 5;
constexpr size_t kPointerBytes = 4;
constexpr uintptr_t kThunk = 0x10004000;

std::vector<Site> ourSites() {
    std::vector<Site> all;
    for (const net_handlers_sites::Set& set : net_handlers_sites::kSets) all.insert(all.end(), set.sites.begin(), set.sites.end());
    return all;
}

bool isCheck(const Site& site) { return site.kind == Kind::Bytes; }

size_t setSize(std::string_view name) {
    for (const net_handlers_sites::Set& set : net_handlers_sites::kSets) {
        if (name == set.name) return set.sites.size();
    }
    return 0;
}

void theRows() {
    const std::vector<Site> sites = ourSites();
    check(sites.size() == 23, "23 rows in 6 sets");
    check(setSize("race hold") == 7 && setSize("replay start") == 1 && setSize("confessional rule") == 5 && setSize("confessional notices") == 7 &&
              setSize("confessional lock") == 2 && setSize("session lost name") == 1,
          "row counts: race hold 7, replay start 1, confessional rule 5, notices 7, lock 2, session lost name 1");
    check(std::size(net_handlers_sites::kSets) == 6, "six sets, in the order of the spec");
    const size_t written = static_cast<size_t>(std::count_if(sites.begin(), sites.end(), [](const Site& site) { return !isCheck(site); }));
    check(written == 13, "13 written code sites, 10 checked rows");

    std::set<std::string_view> ids;
    std::set<uintptr_t> addresses;
    bool unique = true;
    for (const Site& site : sites) unique = unique && ids.insert(site.id).second && addresses.insert(site.address).second;
    check(unique, "ids and addresses are unique");

    bool apart = true;
    for (const Site& a : sites) {
        for (const Site& b : sites) {
            if (&a != &b && other_sites::overlaps(a.address, sizeOf(a), b.address, sizeOf(b))) {
                std::printf("     %s overlaps %s\n", a.id, b.id);
                apart = false;
            }
        }
    }
    check(apart, "no two rows overlap");

    std::set<std::string_view> others;
    for (const Site& site : partner_sites::kSites) others.insert(site.id);
    for (const multiplayer_sites::Phase& phase : multiplayer_sites::kPhases) {
        for (const Site& site : phase.sites) others.insert(site.id);
    }
    for (const session_kick_sites::Set& set : session_kick_sites::kSets) {
        for (const Site& site : set.sites) others.insert(site.id);
    }
    bool freeIds = true;
    for (const Site& site : sites) freeIds = freeIds && others.count(site.id) == 0;
    check(freeIds, "no id is used by the tables of the partner, players 3 and 4 or kick milestones");
}

void theShapes() {
    const std::vector<Site> sites = ourSites();
    bool fit = true;
    for (const Site& site : sites) {
        const auto bytes = partner_sites::parseHex(site.expected);
        if (site.kind == Kind::RetargetCall && (bytes.size() != kBranchBytes || bytes[0] != 0xE8)) fit = false;
        if (site.kind == Kind::JumpOut && bytes.size() < kBranchBytes) fit = false;
        if (site.kind == Kind::HookEntry && bytes.size() < kBranchBytes) fit = false;
        if (site.kind == Kind::CallBlock && bytes.size() != kBranchBytes + partner_sites::parseHex(site.tail).size()) fit = false;
        if (site.kind != Kind::CallBlock && !site.tail.empty()) fit = false;
        if (site.kind == Kind::PointerSwap) fit = false;
    }
    check(fit, "each row has the shape of its kind: a 5 byte call, a jump or hook with room for a branch, a call block whose tail fills the rest");

    const Site* sixth = siteNamed(sites, "CF6");
    const Site* seventh = siteNamed(sites, "CF7");
    check(sixth && sixth->kind == Kind::CallBlock && sizeOf(*sixth) == kBranchBytes && sixth->tail.empty(), "CF6: a 5 byte store becomes a call with no tail");
    check(seventh && seventh->kind == Kind::CallBlock && sizeOf(*seventh) == 10 && seventh->tail == "90 90 90 90 90", "CF7: a 10 byte store becomes a call and 5 nops");

    bool checksWriteNothing = true;
    for (const Site& site : sites) {
        if (isCheck(site)) checksWriteNothing = checksWriteNothing && site.tail.empty() && site.thunk == Thunk::None && partner_replacement::replacementFor(site, 0).empty();
    }
    check(checksWriteNothing, "every check row has an empty tail, no thunk, and writes nothing");

    bool lengths = true;
    for (const Site& site : sites) {
        if (site.kind != Kind::HookEntry && !isCheck(site)) lengths = lengths && partner_replacement::replacementFor(site, kThunk).size() == sizeOf(site);
    }
    check(lengths, "each written row replaces exactly the bytes it checks");

    const std::vector<uint8_t> call = partner_replacement::replacementFor(*seventh, kThunk);
    check(call.size() == 10 && call[0] == 0xE8 && std::all_of(call.begin() + kBranchBytes, call.end(), [](uint8_t byte) { return byte == kNop; }),
          "CF7: a call, then 5 nops");
}

void theTargets() {
    const std::vector<Site> sites = ourSites();
    bool getters = true;
    for (const char* id : {"CF1", "CF2", "CF3", "CF4", "SL1"}) getters = getters && callTarget(*siteNamed(sites, id)) == sr2::kRemotePlayerGetter;
    check(getters, "CF1 to CF4 and SL1 replace the partner getter 0x9D7BD0");
    check(callTarget(*siteNamed(sites, "CF5")) == sr2::kSendBroadcast, "CF5 replaces the broadcast 0x814740 of the host's notice");
    check(siteNamed(sites, "CF1")->thunk == siteNamed(sites, "CF3")->thunk && siteNamed(sites, "CF2")->thunk != siteNamed(sites, "CF4")->thunk,
          "CF1 and CF3 share the farthest remote; CF2 (with the price) and CF4 (the name) are the poorest");
}

void theResumeAddresses() {
    const std::vector<Site> sites = ourSites();
    struct Resume {
        uintptr_t address;
        const char* row;
    };
    const Resume resumes[] = {
        {sr2::kRaceHoldLoad, "RH1r1"},     {sr2::kRaceHoldReturn, "RH1r2"},  {sr2::kRaceHoldUnload, "RH2r1"},   {sr2::kRaceHoldSkip, "RH2r2"},
        {sr2::kConfessShowFar, "CF8r"},    {sr2::kConfessShowPoor, "CF9r1"}, {sr2::kConfessNotCodePoor, "CF9r2"}, {sr2::kConfessReturn, "CFr"},
        {sr2::kRaceResourceRelease, "RHc"}, {sr2::kPriceToCents, "CFc"},
    };
    bool matched = true;
    for (const Resume& resume : resumes) {
        const Site* row = siteNamed(sites, resume.row);
        const bool ok = row && row->address == resume.address && isCheck(*row);
        if (!ok) std::printf("     %s is not the check row of 0x%X\n", resume.row, static_cast<unsigned>(resume.address));
        matched = matched && ok;
    }
    check(matched, "every resume address and called function is the address of a check row");
    check(sr2::kConfessNotCodeFar == 0x7A950F && siteNamed(sites, "CF9")->address == sr2::kConfessNotCodeFar, "the not code 2 resume of CF8 is where CF9 starts");
}

std::vector<other_sites::Range> everyEarlierWrite() {
    std::vector<other_sites::Range> others = other_sites::milestones3to8();
    const auto activities = other_sites::activities();
    const auto crowd = other_sites::players3and4();
    others.insert(others.end(), activities.begin(), activities.end());
    others.insert(others.end(), crowd.begin(), crowd.end());
    const auto add = [&others](const Site& site) { others.push_back({site.address, sizeOf(site)}); };
    for (const auto table : multiplayer_sites::kScriptTraces) {
        for (const Site& site : table) add(site);
    }
    for (const auto table : {std::span<const Site>(trace_sites::kPlayers), std::span<const Site>(trace_sites::kCensus),
                             std::span<const Site>(trace_sites::kCutsceneTrace), std::span<const Site>(trace_sites::kScriptTrace),
                             std::span<const Site>(player_sites::kNetClock), std::span<const Site>(player_sites::kBundleSender),
                             std::span<const Site>(player_sites::kCutsceneVisuals), std::span<const Site>(player_sites::kAnimTrace),
                             std::span<const Site>(vehicle_pool_sites::kHeadroomBlocks), std::span<const Site>(outfit_sync_sites::kLookSync),
                             std::span<const Site>(outfit_sync_sites::kItemWear), std::span<const Site>(outfit_sync_sites::kWeaponSlots),
                             std::span<const Site>(outfit_sync_sites::kTraces)}) {
        for (const Site& site : table) add(site);
    }
    for (const auto& site : player_sites::kSkinPool) others.push_back({site.address, partner_sites::parseHex(site.expected).size()});
    for (const auto& reference : vehicle_pool_references::kReferences) others.push_back({reference.site, kPointerBytes});
    for (const auto& bound : vehicle_pool_sites::kBounds) others.push_back({bound.address, partner_sites::parseHex(bound.expected).size()});
    for (const anim_stream_sites::Phase& phase : anim_stream_sites::kPhases) {
        for (const Site& site : phase.sites) add(site);
        for (const anim_stream_sites::NeedTableSite& site : phase.immediates) others.push_back({site.address, partner_sites::parseHex(site.expected).size()});
    }
    for (const session_kick_sites::Set& set : session_kick_sites::kSets) {
        for (const Site& site : set.sites) add(site);
    }
    return others;
}

void theClearance() {
    const std::vector<other_sites::Range> others = everyEarlierWrite();
    bool clear = true;
    bool clearOfJuiced = true;
    for (const Site& site : ourSites()) {
        if (other_sites::anyOverlap(others, site.address, sizeOf(site))) {
            std::printf("     %s at 0x%X overlaps a site of an earlier milestone\n", site.id, static_cast<unsigned>(site.address));
            clear = false;
        }
        const uintptr_t first = site.address - kClearance;
        const uintptr_t last = site.address + sizeOf(site) + kClearance;
        if (std::any_of(std::begin(juiced::kTargets), std::end(juiced::kTargets), [&](uint32_t target) { return target >= first && target < last; })) {
            std::printf("     %s at 0x%X is within %u bytes of a Juiced target\n", site.id, static_cast<unsigned>(site.address), static_cast<unsigned>(kClearance));
            clearOfJuiced = false;
        }
    }
    check(clear, "no row overlaps a write of the earlier milestones (partner, activities, players 3 and 4, traces, vehicle pool, anim streaming, outfits, kick)");
    check(clearOfJuiced, "every row is 16 bytes clear of an address Juiced touches");
}

}  // namespace

int main() {
    theRows();
    theShapes();
    theTargets();
    theResumeAddresses();
    theClearance();
    return g_failures == 0 ? 0 : 1;
}
