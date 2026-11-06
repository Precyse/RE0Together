#pragma once
// Where a launch keeps its world and its character (DD2 design launcher.md 6; the launcher's campaigns, docs/CONTRACT.md).
// A co-op guest plays the host's world in coop\session and brings the character it picked (arisen=<campaign id>, empty: its
// own save's); a host playing a campaign (campaign=<id>) saves into coop\campaigns\<id>; any other launch keeps the game's own
// save folder. Each world has its character file (profile.dd2p) beside it: coop\profile for the game's own save, the
// campaign's folder for a campaign. Ids come from the launcher; anything but 1 to 64 of [a-z0-9-] is ignored.
#include <algorithm>
#include <optional>
#include <string>

struct Config;

namespace save_folders {

constexpr size_t kMaxIdLength = 64;

inline bool validId(const std::string& id) {
    return !id.empty() && id.size() <= kMaxIdLength &&
           std::all_of(id.begin(), id.end(), [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-'; });
}

struct Folders {
    std::optional<std::wstring> mount;  // the save mount to redirect to; none: the game's own
    std::wstring profile;               // the character file this launch writes (capture) or reads (a guest's splice)
};

// From the config; creates the folders it names.
Folders of(const Config& config);

}  // namespace save_folders
