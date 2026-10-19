#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

// The pure rules of the HUD blocks of milestone 16 wave 2 (docs/SR2_NOTES.md, "HUD and waypoints (milestone 16 wave 2)"; unit
// tested): the turns of a block that runs once per remote player, the remote that holds the back prompt, and the trace text.
namespace hud_rules {

// The turns of one pass over a game block that runs once per remote: `begin` opens the first, `next` the one after it.
class RemoteTurns {
public:
    // Starts a pass over `count` remotes, over any pass that is still running. False and idle for none.
    bool begin(size_t count) {
        count_ = count;
        current_ = 0;
        active_ = count > 0;
        return active_;
    }

    // True while another turn follows; false and idle after the last turn, and when no pass is running.
    bool next() {
        if (!active_) return false;
        if (++current_ < count_) return true;
        active_ = false;
        return false;
    }

    size_t current() const { return current_; }
    bool active() const { return active_; }

private:
    size_t count_ = 0;
    size_t current_ = 0;
    bool active_ = false;
};

// The index of the first of `count` behaviour bytes equal to `behaviour`, `count` when none is.
inline size_t firstWithBehaviour(const uint8_t* behaviours, size_t count, uint8_t behaviour) {
    for (size_t i = 0; i < count; ++i) {
        if (behaviours[i] == behaviour) return i;
    }
    return count;
}

// True when both lists name the same humans in the same order.
inline bool sameHumans(const uintptr_t* a, size_t na, const uintptr_t* b, size_t nb) {
    if (na != nb) return false;
    for (size_t i = 0; i < na; ++i) {
        if (a[i] != b[i]) return false;
    }
    return true;
}

constexpr int32_t kNoFollowerCount = -1;  // the line shows no follower count

// One human of a trace line: its owner slot and, for the minimap, how many followers it has.
struct HumanText {
    uintptr_t human;
    uint8_t slot;
    int32_t followers;
};

// "0x1A2B0000 (slot 1, 3 followers), 0x1A2C0000 (slot 2, 0 followers)", or without the count for kNoFollowerCount.
inline std::string humansLine(const HumanText* humans, size_t count) {
    std::string line;
    for (size_t i = 0; i < count; ++i) {
        char part[64];
        if (humans[i].followers == kNoFollowerCount) {
            std::snprintf(part, sizeof(part), "%s0x%08X (slot %u)", i == 0 ? "" : ", ", static_cast<unsigned>(humans[i].human),
                          static_cast<unsigned>(humans[i].slot));
        } else {
            std::snprintf(part, sizeof(part), "%s0x%08X (slot %u, %d followers)", i == 0 ? "" : ", ", static_cast<unsigned>(humans[i].human),
                          static_cast<unsigned>(humans[i].slot), static_cast<int>(humans[i].followers));
        }
        line += part;
    }
    return line;
}

}  // namespace hud_rules
