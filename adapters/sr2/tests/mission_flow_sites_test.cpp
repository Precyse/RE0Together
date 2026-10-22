// The milestone 16 W2 mission flow site tables (no game): ids and addresses, shapes, the callee of each retargeted call, the phases
// they are in and clearance from every other site and from everything Juiced touches.
#include <algorithm>
#include <cstdio>
#include <set>
#include <span>
#include <string_view>
#include <vector>

#include "../src/mission_flow_sites.h"
#include "../src/multiplayer_sites.h"
#include "../src/outfit_sync_sites.h"
#include "../src/partner_replacement.h"
#include "../src/sr2_addresses.h"
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

constexpr uintptr_t kJuicedMargin = 16;
constexpr size_t kBranchBytes = 5;
constexpr size_t kDwordBytes = 4;
constexpr size_t kCallBlockBytes = 10;
constexpr size_t kJumpOutBytes = 12;
constexpr size_t kTotalSites = 23;
constexpr size_t kWrittenSites = 22;  // MF15c only checks bytes
constexpr uintptr_t kThunk = 0x10004000;
constexpr uint8_t kCallOpcode = 0xE8;

std::vector<Site> everySite() {
    std::vector<Site> all;
    for (const auto table : {std::span<const Site>(mission_flow_sites::kPrompts), std::span<const Site>(mission_flow_sites::kReplay),
                             std::span<const Site>(mission_flow_sites::kRewards), std::span<const Site>(mission_flow_sites::kTrace)}) {
        all.insert(all.end(), table.begin(), table.end());
    }
    return all;
}

size_t phaseSize(std::string_view name) {
    for (const multiplayer_sites::Phase& phase : multiplayer_sites::kPhases) {
        if (name == phase.name) return phase.sites.size();
    }
    return 0;
}

bool phaseIsTraceOnly(std::string_view name) {
    for (const multiplayer_sites::Phase& phase : multiplayer_sites::kPhases) {
        if (name == phase.name) return phase.traceOnly;
    }
    return false;
}

void theTables() {
    const std::vector<Site> all = everySite();
    std::set<std::string_view> ids;
    std::set<uintptr_t> addresses;
    for (const Site& site : all) {
        ids.insert(site.id);
        addresses.insert(site.address);
    }
    check(all.size() == kTotalSites && ids.size() == kTotalSites && addresses.size() == kTotalSites, "23 sites with unique ids and addresses (MF22 is OF6)");
    check(static_cast<size_t>(std::count_if(all.begin(), all.end(), [](const Site& site) { return site.kind != Kind::Bytes; })) == kWrittenSites,
          "22 of them are written, MF15c only checks bytes");
    check(siteNamed(all, "MF22") == nullptr, "MF22 is not repeated, outfit_sync_sites.h writes 0x53FF08 as OF6");
    check(std::any_of(std::begin(outfit_sync_sites::kLookSync), std::end(outfit_sync_sites::kLookSync),
                      [](const Site& site) { return site.address == 0x53FF08 && site.thunk == Thunk::EachRemoteMember82BC50; }),
          "OF6 is the call at 0x53FF08 with the look sync thunk MF22 asked for");
    check(std::all_of(all.begin(), all.end(), [](const Site& site) { return (site.kind == Kind::Bytes) == (site.thunk == Thunk::None); }),
          "every written site has a thunk, the check has none");
    check(phaseSize("mission prompts") == 11 && phaseSize("replay prompt") == 7 && phaseSize("mission rewards") == 4 &&
              phaseSize("mission flow trace") == 1,
          "the phases hold 11, 7, 4 and 1 sites");
    check(phaseIsTraceOnly("mission flow trace") && !phaseIsTraceOnly("mission prompts") && !phaseIsTraceOnly("replay prompt") &&
              !phaseIsTraceOnly("mission rewards"),
          "only the trace phase waits for trace=1");
}

void theShapes() {
    const std::vector<Site> all = everySite();
    bool blocks = true;
    for (const Site& site : all) {
        if (site.kind != Kind::CallBlock) continue;
        const auto written = partner_replacement::replacementFor(site, kThunk);
        blocks = blocks && sizeOf(site) == kCallBlockBytes && written.size() == kCallBlockBytes && written[0] == kCallOpcode &&
                 std::all_of(written.begin() + kBranchBytes, written.end(), [](uint8_t byte) { return byte == kNop; });
    }
    check(blocks, "the call blocks MF11 and MF17 are 10 bytes: a call to the thunk and 5 nops");
    check(siteNamed(all, "MF11")->kind == Kind::CallBlock && siteNamed(all, "MF17")->kind == Kind::CallBlock,
          "MF11 and MF17 are the call blocks");
    check(siteNamed(all, "MF6")->kind == Kind::JumpOut && sizeOf(*siteNamed(all, "MF6")) == kJumpOutBytes, "MF6 is a 12 byte jump out");
    bool calls = true;
    for (const Site& site : all) {
        if (site.kind != Kind::RetargetCall) continue;
        const auto bytes = partner_sites::parseHex(site.expected);
        calls = calls && bytes.size() == kBranchBytes && bytes[0] == kCallOpcode;
    }
    check(calls, "every retargeted call is a 5 byte call");
    bool hooks = true;
    for (const Site& site : all) {
        if (site.kind == Kind::HookEntry) hooks = hooks && sizeOf(site) >= kBranchBytes;
    }
    check(hooks, "every hook covers at least 5 bytes");
}

void theCallees() {
    const std::vector<Site> all = everySite();
    const auto callee = [&](std::string_view id) { return callTarget(*siteNamed(all, id)); };
    bool getters = true;
    for (const char* id : {"MF2", "MF3", "MF7", "MF8", "MF9", "MF10", "MF11", "MF21"}) getters = getters && callee(id) == sr2::kRemotePlayerGetter;
    check(getters, "MF2, MF3, MF7 to MF11 and MF21 call get_remote_player 0x9D7BD0");
    check(callee("MF4") == sr2::kStartRequest, "MF4 calls the start request 0x6A8680");
    bool broadcasts = true;
    for (const char* id : {"MF5", "MF12", "MF14", "MF16", "MF19"}) broadcasts = broadcasts && callee(id) == sr2::kSendBroadcast;
    check(broadcasts, "MF5, MF12, MF14, MF16 and MF19 call the broadcast 0x814740");
    check(callee("MF17") == sr2::kPartnerMemberGetter, "MF17 calls the partner member getter 0x8CA380");
    check(callee("MF20") == sr2::kBonusAward, "MF20 calls the bonus function 0x6A9640");
    check(siteNamed(all, "MF18")->address == sr2::kBonusAward, "MF18 hooks the bonus function 0x6A9640");
    const Site* cancel = siteNamed(all, "MF15c");
    check(cancel->kind == Kind::Bytes && cancel->address == sr2::kReplayCancel && std::string_view(cancel->tail).empty(),
          "MF15c checks the bytes of the replay wait cancel 0x544F10 and writes nothing");
    check(siteNamed(all, "MF1")->address == 0x6A8450 && siteNamed(all, "MF13")->address == 0x543D70 && siteNamed(all, "MF15")->address == 0x543F10,
          "MF1, MF13 and MF15 hook the start prompt callback and the replay ask and answer handlers");
}

std::vector<other_sites::Range> otherRanges() {
    std::vector<other_sites::Range> others = other_sites::milestones3to8();
    const auto activities = other_sites::activities();
    others.insert(others.end(), activities.begin(), activities.end());
    for (const multiplayer_sites::Phase& phase : multiplayer_sites::kPhases) {
        for (const Site& site : phase.sites) {
            if (!siteNamed(everySite(), site.id)) others.push_back({site.address, sizeOf(site)});
        }
    }
    for (const auto table : multiplayer_sites::kScriptTraces) {
        for (const Site& site : table) others.push_back({site.address, sizeOf(site)});
    }
    for (const auto& row : vehicle_pool_references::kReferences) others.push_back({row.site, kDwordBytes});
    for (const auto& bound : vehicle_pool_sites::kBounds) others.push_back({bound.address, partner_sites::parseHex(bound.expected).size()});
    for (const Site& site : vehicle_pool_sites::kHeadroomBlocks) others.push_back({site.address, sizeOf(site)});
    for (const auto table : {std::span<const Site>(outfit_sync_sites::kLookSync), std::span<const Site>(outfit_sync_sites::kItemWear),
                             std::span<const Site>(outfit_sync_sites::kWeaponSlots), std::span<const Site>(outfit_sync_sites::kTraces)}) {
        for (const Site& site : table) others.push_back({site.address, sizeOf(site)});
    }
    return others;
}

void theClearance() {
    const std::vector<Site> all = everySite();
    bool apart = true;
    for (const Site& a : all) {
        for (const Site& b : all) {
            if (&a != &b && other_sites::overlaps(a.address, sizeOf(a), b.address, sizeOf(b))) apart = false;
        }
    }
    check(apart, "no two sites overlap");

    const std::vector<other_sites::Range> others = otherRanges();
    bool clear = true;
    bool clearOfJuiced = true;
    for (const Site& site : all) {
        if (other_sites::anyOverlap(others, site.address, sizeOf(site))) {
            std::printf("     %s at 0x%X overlaps a site of another table\n", site.id, static_cast<unsigned>(site.address));
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
    check(clear, "no site overlaps a site of another table");
    check(clearOfJuiced, "no site is within 16 bytes of a Juiced target");
}

}  // namespace

int main() {
    theTables();
    theShapes();
    theCallees();
    theClearance();
    return g_failures == 0 ? 0 : 1;
}
