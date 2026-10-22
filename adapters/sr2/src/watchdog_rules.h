#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

// The pure rules of the game thread watchdog (docs/SR2_NOTES.md, "Watchdog"): when a stall is reported, when it is over, which
// dumps are kept and how a dump is named.
namespace watchdog_rules {

constexpr uint64_t kStallMs = 10000;  // the game's frame (0x697780) has not run for this long
constexpr size_t kKeptDumps = 3;

enum class Event { None, Stalled, Resumed };

// Fed the time now and the time of the last frame (0 before the first one). A stall is reported once, and its end once.
class Monitor {
public:
    Event step(uint64_t now, uint64_t lastFrame) {
        if (lastFrame == 0) return Event::None;
        const bool stalled = now >= lastFrame && now - lastFrame >= kStallMs;
        if (stalled && !reported_) {
            reported_ = true;
            stalledSince_ = lastFrame;
            return Event::Stalled;
        }
        if (!stalled && reported_) {
            reported_ = false;
            stalledFor_ = lastFrame - stalledSince_;
            return Event::Resumed;
        }
        return Event::None;
    }

    // How long the stall that just ended lasted (the frame that ended it minus the last frame before it).
    uint64_t stalledFor() const { return stalledFor_; }

private:
    bool reported_ = false;
    uint64_t stalledSince_ = 0;
    uint64_t stalledFor_ = 0;
};

// The dump stems to delete so that one more dump leaves at most `keep`: the oldest first (stems sort by time).
inline std::vector<std::string> stemsToDelete(std::vector<std::string> stems, size_t keep) {
    std::sort(stems.begin(), stems.end());
    const size_t room = keep > 0 ? keep - 1 : 0;
    if (stems.size() <= room) return {};
    stems.resize(stems.size() - room);
    return stems;
}

// "20261022-004925": the local time of a dump, which sorts by time.
inline std::string stem(unsigned year, unsigned month, unsigned day, unsigned hour, unsigned minute, unsigned second) {
    char text[32];
    std::snprintf(text, sizeof(text), "%04u%02u%02u-%02u%02u%02u", year, month, day, hour, minute, second);
    return text;
}

}  // namespace watchdog_rules
