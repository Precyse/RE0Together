#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "sr2_addresses.h"

// The pure rules of the waypoint recipient scope of milestone 16 wave 2 (docs/SR2_NOTES.md, "HUD and waypoints (milestone 16
// wave 2)"; unit tested): the control text a script passes to mission_debug, the name it opens the scope for, and the layout of
// the Lua stack the adapter reads that text from.
namespace waypoint_scope_rules {

constexpr std::string_view kSendToControl = "coop_send_to";
constexpr size_t kMaxNameBytes = 32;
// The longest control text: the word, a space and a name. Reading one character more tells a longer text from this one.
constexpr size_t kMaxControlBytes = kSendToControl.size() + 1 + kMaxNameBytes;

constexpr char kFirstPrintable = 0x20;
constexpr char kLastPrintable = 0x7E;

// What a debug text means to the scope: not a control, the close (`coop_send_to`) or the opening for a name.
struct Control {
    bool isControl;
    bool open;
    std::string_view name;
};

inline bool isPrintable(char c) { return c >= kFirstPrintable && c <= kLastPrintable; }

// "coop_send_to" closes, "coop_send_to #PLAYER3#" opens for that name; anything else, an empty or long name and a name with
// a byte that is not printable included, is no control.
inline Control parseControl(std::string_view text) {
    constexpr Control kNotControl{false, false, {}};
    if (text.substr(0, kSendToControl.size()) != kSendToControl) return kNotControl;
    if (text.size() == kSendToControl.size()) return {true, false, {}};
    if (text[kSendToControl.size()] != ' ') return kNotControl;
    const std::string_view name = text.substr(kSendToControl.size() + 1);
    if (name.empty() || name.size() > kMaxNameBytes || !std::all_of(name.begin(), name.end(), isPrintable)) return kNotControl;
    return {true, true, name};
}

// The index of the first of `count` names equal to `name` (exact, case sensitive), `count` when none is.
inline size_t findName(const std::string_view* names, size_t count, std::string_view name) {
    for (size_t i = 0; i < count; ++i) {
        if (names[i] == name) return i;
    }
    return count;
}

// The player the waypoint senders answer to now, if any. Holds a copy of the name, so a player that left cannot be sent to.
class Scope {
public:
    // Opens the scope for `name` (at most kMaxNameBytes bytes are kept). True when it replaced a scope that was still open.
    bool open(std::string_view name) {
        const bool replaced = active_;
        length_ = std::min(name.size(), kMaxNameBytes);
        std::copy_n(name.begin(), length_, name_);
        active_ = true;
        return replaced;
    }

    void close() {
        active_ = false;
        length_ = 0;
    }

    bool active() const { return active_; }
    std::string_view name() const { return {name_, length_}; }

private:
    char name_[kMaxNameBytes] = {};
    size_t length_ = 0;
    bool active_ = false;
};

// The Lua stack of the game (Lua 5.0): a stack object is 16 bytes, the type word first and the value at +8.
constexpr bool isLuaString(uint32_t type) { return type == sr2::kLuaTypeString; }

// True when the running native was passed an argument: the stack has an object between its base and its top.
constexpr bool hasArgument(uint32_t base, uint32_t top) { return top >= base + sr2::kLuaObjectBytes; }

}  // namespace waypoint_scope_rules
