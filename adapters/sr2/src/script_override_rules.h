#pragma once
#include <set>
#include <string>
#include <string_view>

#include "ascii_text.h"

// The pure rule of the Lua script overrides (milestone 7, no game access, unit tested): a script the game loads by name
// is read from <game>\coop\scripts instead when a file of that name is there.
namespace script_override_rules {

// The folder, relative to the game folder, that the game opens an override from.
inline constexpr std::string_view kFolder = "coop\\scripts\\";

// The last component of a path with / or \ separators.
inline std::string_view fileNameOf(std::string_view path) {
    const size_t at = path.find_last_of("/\\");
    return at == std::string_view::npos ? path : path.substr(at + 1);
}

// The path to open instead of `path`, empty when no override has that file name. `names` holds lower case file names.
inline std::string overridePath(std::string_view path, const std::set<std::string>& names) {
    const std::string_view name = fileNameOf(path);
    if (name.empty() || !names.contains(ascii_text::lowered(name))) return {};
    return std::string(kFolder) + std::string(name);
}

}  // namespace script_override_rules
