// Every range of the exe that a milestone before the one under test writes, for the clearance checks of the site tables.
#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include "../src/activity_sites.h"
#include "../src/crowd_control_sites.h"
#include "../src/diversion_sites.h"
#include "../src/force_join_sites.h"
#include "../src/game_port_patches.h"
#include "../src/join_net_trace_sites.h"
#include "../src/lan_address_patches.h"
#include "../src/multiplayer_sites.h"
#include "../src/partner_sites.h"
#include "../src/player_cap_patches.h"
#include "../src/replication_sites.h"
#include "../src/version_gate_patches.h"
#include "../src/walk_sites.h"

namespace other_sites {

using Range = std::pair<uintptr_t, size_t>;  // address, size

constexpr uintptr_t kAnyThunk = 0x10002000;
constexpr uint32_t kManyPlayers = 4;
constexpr uint32_t kMovedPort = 4201;

inline bool overlaps(uintptr_t a, size_t aSize, uintptr_t b, size_t bSize) { return a < b + bSize && b < a + aSize; }

// What milestone 6 touches.
inline std::vector<Range> forceJoin() {
    std::vector<Range> ranges;
    for (const force_join_sites::Site& site : force_join_sites::kSites) {
        ranges.push_back({site.address, partner_sites::parseHex(site.expected).size()});
    }
    return ranges;
}

// What the tables of milestones 3, 4, 5, 5b and 8 write, with every optional (trace, moved port) site included.
inline std::vector<Range> withoutForceJoin() {
    std::vector<Range> ranges;
    for (const partner_sites::Site& site : partner_sites::kSites) {
        ranges.push_back({site.address, partner_sites::parseHex(site.expected).size()});
    }
    for (const auto& entry : replication_sites::kEntries) ranges.push_back({entry.n1, replication_sites::kEntryProbeBytes});
    for (const auto& branch : replication_sites::kBranches) ranges.push_back({branch.address, 2});
    for (const auto& trace : join_net_trace_sites::kSites) {
        ranges.push_back({trace.address, partner_sites::parseHex(trace.expected).size()});
    }
    const player_cap_patches::Thunks capThunks{kAnyThunk, kAnyThunk, kAnyThunk, kAnyThunk, kAnyThunk, kAnyThunk, kAnyThunk};
    const game_port_patches::Thunks portThunks{kAnyThunk, kAnyThunk, kAnyThunk, kAnyThunk};
    const version_gate_patches::Thunks gateThunks{kAnyThunk, kAnyThunk, kAnyThunk, kAnyThunk, kAnyThunk};
    for (const auto& list : {player_cap_patches::forPlayers(kManyPlayers, capThunks, true),
                             game_port_patches::forPort(kMovedPort, kManyPlayers, true, Transport::Native, portThunks),
                             lan_address_patches::forPlayers(kManyPlayers, kAnyThunk), version_gate_patches::patches(gateThunks)}) {
        for (const CodePatch& patch : list) {
            if (!patch.replacement.empty()) ranges.push_back({patch.address, patch.expected.size()});
        }
    }
    return ranges;
}

// What the tables of milestones 3 to 8 write.
inline std::vector<Range> milestones3to8() {
    std::vector<Range> ranges = withoutForceJoin();
    const std::vector<Range> forceJoinRanges = forceJoin();
    ranges.insert(ranges.end(), forceJoinRanges.begin(), forceJoinRanges.end());
    return ranges;
}

// What milestone 7, milestone 16 wave 2 (the diversions) and milestone 17 wave 3 (Crowd Control and the walks) write.
inline std::vector<Range> activities() {
    std::vector<Range> ranges;
    for (const auto table : {std::span<const partner_sites::Site>(activity_sites::kPhase1),
                             std::span<const partner_sites::Site>(activity_sites::kPhase2),
                             std::span<const partner_sites::Site>(activity_sites::kPhase3)}) {
        for (const partner_sites::Site& site : table) ranges.push_back({site.address, partner_sites::parseHex(site.expected).size()});
    }
    for (const diversion_sites::Set& set : diversion_sites::kSets) {
        for (const partner_sites::Site& site : set.sites) ranges.push_back({site.address, partner_sites::parseHex(site.expected).size()});
    }
    for (const crowd_control_sites::Set& set : crowd_control_sites::kSets) {
        for (const partner_sites::Site& site : set.sites) ranges.push_back({site.address, partner_sites::parseHex(site.expected).size()});
    }
    for (const walk_sites::Set& set : walk_sites::kSets) {
        for (const partner_sites::Site& site : set.sites) ranges.push_back({site.address, partner_sites::parseHex(site.expected).size()});
    }
    return ranges;
}

// What milestone 9 writes.
inline std::vector<Range> players3and4() {
    std::vector<Range> ranges;
    for (const multiplayer_sites::Phase& phase : multiplayer_sites::kPhases) {
        for (const partner_sites::Site& site : phase.sites) ranges.push_back({site.address, partner_sites::parseHex(site.expected).size()});
    }
    return ranges;
}

inline bool anyOverlap(const std::vector<Range>& ranges, uintptr_t address, size_t size) {
    for (const auto& [otherAddress, otherSize] : ranges) {
        if (overlaps(address, size, otherAddress, otherSize)) return true;
    }
    return false;
}

}  // namespace other_sites
