#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

// SLOW_FRAME decision (pure, unit tested): a frame is the time between two ticks of the controlled player's move; the
// callbacks, move scopes and post-move steps that ran in it add their time per category, and a frame of
// kSlowFrameUs or more is reported, at most once per kLogIntervalUs. The clock is the caller's, in microseconds.
namespace slow_frame {

enum class Category : size_t { Net, Puppet, Enemies, Events, Fx, Other, Count };

constexpr size_t kCategoryCount = static_cast<size_t>(Category::Count);
constexpr uint64_t kSlowFrameUs = 50'000;
constexpr uint64_t kLogIntervalUs = 5'000'000;
constexpr uint64_t kUsPerMs = 1000;

// Which line column a game_tick entry belongs to, by its registered name (prefix match); unlisted names are Other.
struct CategoryPrefix {
    std::string_view prefix;
    Category category;
};

constexpr std::array<CategoryPrefix, 14> kCategoryPrefixes{{
    {"puppet", Category::Puppet},
    {"pose_", Category::Puppet},
    {"state_correction", Category::Puppet},
    {"post_move", Category::Puppet},
    {"enemy_", Category::Enemies},
    {"weapon_fx", Category::Fx},
    {"event_", Category::Events},
    {"door_", Category::Events},
    {"room_", Category::Events},
    {"split_rooms", Category::Events},
    {"flag_", Category::Events},
    {"net_", Category::Net},
    {"join_sync", Category::Net},
    {"resync", Category::Net},
}};

constexpr Category categoryOf(std::string_view name) {
    for (const CategoryPrefix& entry : kCategoryPrefixes) {
        if (name.substr(0, entry.prefix.size()) == entry.prefix) return entry.category;
    }
    return Category::Other;
}

struct Report {
    uint32_t frameMs = 0;
    std::array<uint32_t, kCategoryCount> categoryMs{};
};

class Tracker {
public:
    // Adds time spent inside the current frame.
    void add(Category category, uint64_t us) { sumUs_[static_cast<size_t>(category)] += us; }

    // Closes the previous frame and opens the next at `nowUs`. True when the closed frame was slow and a report is due;
    // report() then holds its numbers until the next call.
    bool beginFrame(uint64_t nowUs) {
        const bool due = hasFrame_ && nowUs - frameStartUs_ >= kSlowFrameUs && logAllowed(nowUs);
        if (due) {
            report_.frameMs = static_cast<uint32_t>((nowUs - frameStartUs_) / kUsPerMs);
            for (size_t i = 0; i < kCategoryCount; ++i) report_.categoryMs[i] = static_cast<uint32_t>(sumUs_[i] / kUsPerMs);
            lastLogUs_ = nowUs;
            logged_ = true;
        }
        sumUs_.fill(0);
        frameStartUs_ = nowUs;
        hasFrame_ = true;
        return due;
    }

    const Report& report() const { return report_; }

private:
    bool logAllowed(uint64_t nowUs) const { return !logged_ || nowUs - lastLogUs_ >= kLogIntervalUs; }

    std::array<uint64_t, kCategoryCount> sumUs_{};
    Report report_;
    uint64_t frameStartUs_ = 0;
    uint64_t lastLogUs_ = 0;
    bool hasFrame_ = false;
    bool logged_ = false;
};

}  // namespace slow_frame
