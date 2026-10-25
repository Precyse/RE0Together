#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// The text of the developer command file coop\cmd.txt (adapter.ini dev_commands=1): one command per line. Pure, no game
// access, unit tested.
namespace dev_command_rule {

constexpr uint32_t kMaxStallMs = 60'000;

enum class Kind : uint8_t { Stall, Lock, Rescue };

struct Command {
    Kind kind;
    uint32_t stallMs;  // Stall only
};

struct Parsed {
    std::vector<Command> commands;
    std::vector<std::string> refused;  // the lines that are not a command, trimmed
};

constexpr const char* name(Kind kind) {
    switch (kind) {
        case Kind::Stall: return "stall";
        case Kind::Lock: return "lock";
        case Kind::Rescue: return "rescue";
    }
    return "unknown";
}

namespace detail {

constexpr std::string_view kBlanks = " \t\r";

inline std::string_view trim(std::string_view text) {
    const size_t begin = text.find_first_not_of(kBlanks);
    if (begin == std::string_view::npos) return {};
    return text.substr(begin, text.find_last_not_of(kBlanks) - begin + 1);
}

// "stall <ms>" argument: decimal digits only, 1..kMaxStallMs.
inline bool parseStallMs(std::string_view digits, uint32_t& out) {
    if (digits.empty() || digits.size() > 5) return false;
    uint32_t value = 0;
    for (const char c : digits) {
        if (c < '0' || c > '9') return false;
        value = value * 10 + static_cast<uint32_t>(c - '0');
    }
    if (value < 1 || value > kMaxStallMs) return false;
    out = value;
    return true;
}

inline bool parseLine(std::string_view line, Command& out) {
    const size_t blank = line.find_first_of(kBlanks);
    const std::string_view word = line.substr(0, blank);
    const std::string_view argument = blank == std::string_view::npos ? std::string_view{} : trim(line.substr(blank));
    if (word == name(Kind::Stall)) {
        out = {Kind::Stall, 0};
        return parseStallMs(argument, out.stallMs);
    }
    for (const Kind kind : {Kind::Lock, Kind::Rescue}) {
        if (word == name(kind) && argument.empty()) {
            out = {kind, 0};
            return true;
        }
    }
    return false;
}

}  // namespace detail

// Every non-blank line of `text`: a command, or kept in `refused`.
inline Parsed parse(std::string_view text) {
    Parsed parsed;
    while (!text.empty()) {
        const size_t end = text.find('\n');
        const std::string_view line = detail::trim(text.substr(0, end));
        text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
        if (line.empty()) continue;
        Command command{};
        if (detail::parseLine(line, command)) parsed.commands.push_back(command);
        else parsed.refused.emplace_back(line);
    }
    return parsed;
}

}  // namespace dev_command_rule
