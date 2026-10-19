#pragma once
#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>

#include "rate_limit.h"

// The pure rules of the milestone 13 traces (no game, unit tested): when the fade tick has something worth a line, how a
// text read from the game is cleaned, which slots a trace line names, and the per-name limit of the Lua error lines.
namespace script_trace_rules {

constexpr uint32_t kFadeNone = 0;
constexpr size_t kMaxErrorNames = 32;

struct FadeSnapshot {
    uint32_t state = kFadeNone;  // 0 none, 1 out, 2 in
    float percent = 0;
    int32_t timer = -1;  // -1 when no fade runs
    int32_t pauseCount = 0;
    uint8_t ignorePause = 0;
};

// The fade does not move while the world is paused (the pause count is above zero) unless it ignores the pause.
inline bool heldByPause(const FadeSnapshot& fade) {
    return fade.state != kFadeNone && fade.timer >= 0 && fade.pauseCount > 0 && fade.ignorePause == 0;
}

struct FadeEdges {
    bool stateChanged = false;
    uint32_t stateBefore = kFadeNone;
    bool complete = false;  // a running fade ended
    bool held = false;      // the fade began to wait for the pause count
    bool released = false;  // it stopped waiting
};

// Compares each fade tick with the one before and reports only the changes.
class FadeWatch {
public:
    FadeEdges observe(const FadeSnapshot& now) {
        FadeEdges edges;
        edges.stateChanged = now.state != last_.state;
        edges.stateBefore = last_.state;
        edges.complete = last_.timer >= 0 && now.timer < 0;
        const bool holds = heldByPause(now);
        edges.held = holds && !held_;
        edges.released = !holds && held_;
        held_ = holds;
        last_ = now;
        return edges;
    }

private:
    FadeSnapshot last_;
    bool held_ = false;
};

// `raw` cut at the first NUL or after `limit` characters, a control character shown as '?'.
inline std::string clipped(const char* raw, size_t size, size_t limit) {
    std::string text;
    for (size_t i = 0; i < size && i < limit && raw[i] != '\0'; ++i) {
        const unsigned char c = static_cast<unsigned char>(raw[i]);
        text.push_back(c < ' ' || c == 0x7F ? '?' : static_cast<char>(c));
    }
    return text;
}

// True when `raw` is a non-empty text of printable ASCII up to its NUL (the name of a thread, to be logged as it is).
inline bool isPrintableName(const char* raw, size_t size) {
    if (size == 0 || raw[0] == '\0') return false;
    for (size_t i = 0; i < size && raw[i] != '\0'; ++i) {
        if (raw[i] < ' ' || raw[i] > '~') return false;
    }
    return true;
}

// "{1,3}" for the slots, "{}" for none.
inline std::string slotListText(const uint8_t* slots, size_t count) {
    std::string text = "{";
    for (size_t i = 0; i < count; ++i) {
        if (i > 0) text += ',';
        text += std::to_string(slots[i]);
    }
    return text + '}';
}

// One line per name per second. Bounded: when it is full, the name that was seen first is forgotten.
class NamedRateLimit {
public:
    // True when a line for `name` may be written at `nowMs`; `triggers` then holds the triggers since its last line.
    bool allow(const std::string& name, uint64_t nowMs, uint32_t& triggers) {
        for (Entry& entry : entries_) {
            if (entry.name == name) return entry.limit.allow(nowMs, triggers);
        }
        if (entries_.size() == kMaxErrorNames) entries_.pop_front();
        entries_.push_back({name, rate_limit::RateLimit()});
        return entries_.back().limit.allow(nowMs, triggers);
    }

private:
    struct Entry {
        std::string name;
        rate_limit::RateLimit limit;
    };
    std::deque<Entry> entries_;
};

}  // namespace script_trace_rules
