#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "force_join_names.h"

// Finding the host in the server browser's list (pure, unit tested). The join function takes an index that counts only
// the entries the game lists (byte +0x65 set), in list order, as the game's own copy 0x8D0A50 does.
namespace browser_match {

// One browser entry as read from the game.
struct Entry {
    uintptr_t address;
    bool listed;
    std::string name;  // online_name, read for listed entries only
};

struct Match {
    size_t index;      // among the listed entries: what the join function takes
    uintptr_t address; // the entry
};

inline size_t listedCount(const std::vector<Entry>& entries) {
    size_t count = 0;
    for (const Entry& entry : entries) count += entry.listed ? 1 : 0;
    return count;
}

// The first listed entry whose name holds the tag of `token`.
inline std::optional<Match> find(const std::vector<Entry>& entries, const std::string& token) {
    size_t index = 0;
    for (const Entry& entry : entries) {
        if (!entry.listed) continue;
        if (force_join_names::containsTag(entry.name, token)) return Match{index, entry.address};
        ++index;
    }
    return std::nullopt;
}

}  // namespace browser_match
