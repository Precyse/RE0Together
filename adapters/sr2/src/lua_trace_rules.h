#pragma once
#include <cstddef>
#include <cstring>
#include <string>

#include "script_trace_rules.h"

// The pure rules of the trace lines the mission scripts write (trace=1 only, docs/SR2_NOTES.md "Lua per-remote slots and LT1
// (M16 wave 2)"; unit tested): the Lua library (tools/sr2/lua/coop_slots.lua, coop_trace) passes "coop: <text>" to the no-op
// native mission_debug, and the adapter logs the text.
namespace lua_trace_rules {

constexpr char kCoopPrefix[] = "coop: ";
constexpr size_t kCoopPrefixLength = sizeof(kCoopPrefix) - 1;
constexpr size_t kLineLimit = 200;     // characters of the text that are logged
constexpr size_t kRateKeyLength = 64;  // characters that tell one line from another for the once-a-second limit

// The text after "coop: " in the `size` readable characters of `text`, cut at the first NUL or after kLineLimit characters, a
// control character shown as '?'; empty when `text` is not a coop line.
inline std::string coopLine(const char* text, size_t size) {
    if (size < kCoopPrefixLength || std::memcmp(text, kCoopPrefix, kCoopPrefixLength) != 0) return {};
    return script_trace_rules::clipped(text + kCoopPrefixLength, size - kCoopPrefixLength, kLineLimit);
}

// Lines with the same first kRateKeyLength characters count as identical (a seat or slot line names the player and vehicle first).
inline std::string rateKey(const std::string& line) { return line.substr(0, kRateKeyLength); }

}  // namespace lua_trace_rules
