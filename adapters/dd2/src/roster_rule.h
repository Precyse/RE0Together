#pragma once
// The enemy roster key and the guest's comparison (pure, unit tested; DD2 design world.md "Identity"): a table-placed
// enemy carries the same UniqueID (row Guid + index) on both machines, so the host's roster and the guest's own can be
// matched key by key (static_checks.md P7-1: the key is the generate row's Guid and the spawn's number in it; which rows
// spawn is each machine's own lottery). The guest binds the keys in both, destroys its extras and asks for the missing.
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

namespace roster_rule {

#pragma pack(push, 1)
struct Key {
    std::array<uint8_t, 16> rowId{};  // UniqueID._RowID (Guid)
    int32_t index = 0;                // UniqueID._Index

    bool operator==(const Key& other) const { return rowId == other.rowId && index == other.index; }
    bool operator<(const Key& other) const { return rowId != other.rowId ? rowId < other.rowId : index < other.index; }
    bool empty() const { return index == 0 && std::all_of(rowId.begin(), rowId.end(), [](uint8_t b) { return b == 0; }); }
};
#pragma pack(pop)
static_assert(sizeof(Key) == 20);

struct Comparison {
    std::vector<Key> both;
    std::vector<Key> extra;    // only in ours (our lottery spawned it; the host's world has none)
    std::vector<Key> missing;  // only in the host's

    bool operator==(const Comparison& other) const {
        return both == other.both && extra == other.extra && missing == other.missing;
    }
};

// Both lists in any order, duplicates allowed (listed once each); the results are sorted.
inline Comparison compare(std::vector<Key> host, std::vector<Key> own) {
    std::sort(host.begin(), host.end());
    host.erase(std::unique(host.begin(), host.end()), host.end());
    std::sort(own.begin(), own.end());
    own.erase(std::unique(own.begin(), own.end()), own.end());
    Comparison out;
    size_t h = 0, o = 0;
    while (h < host.size() || o < own.size()) {
        if (o == own.size() || (h < host.size() && host[h] < own[o])) {
            out.missing.push_back(host[h++]);
        } else if (h == host.size() || own[o] < host[h]) {
            out.extra.push_back(own[o++]);
        } else {
            out.both.push_back(host[h++]);
            ++o;
        }
    }
    return out;
}

}  // namespace roster_rule
