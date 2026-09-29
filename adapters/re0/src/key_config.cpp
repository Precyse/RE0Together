#include "key_config.h"

#include <windows.h>

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

namespace {

constexpr std::string_view kNamePrefix = "KB_";
constexpr std::string_view kChangeKey = "KC_change";
constexpr std::string_view kTraceKey = "KC_trace";
constexpr char kIniRelativePath[] = "/CAPCOM/RESIDENT EVIL 0 HD REMASTER/config.ini";

struct NamedKey {
    std::string_view name;
    int code;
};

constexpr NamedKey kNamedKeys[] = {{"SPACE", VK_SPACE}, {"SHIFT", VK_SHIFT}, {"TAB", VK_TAB},
                                   {"UP", VK_UP},       {"DOWN", VK_DOWN},   {"LEFT", VK_LEFT},
                                   {"RIGHT", VK_RIGHT}};

std::string_view trim(std::string_view text) {
    const auto begin = text.find_first_not_of(" \t\r");
    if (begin == std::string_view::npos) return {};
    return text.substr(begin, text.find_last_not_of(" \t\r") - begin + 1);
}

// The keyboard binding of `key` (the first line of any section whose value is a known "KB_" name), or `fallback`.
int keyOrDefault(std::string_view iniText, std::string_view key, int fallback) {
    while (!iniText.empty()) {
        const auto end = iniText.find('\n');
        const std::string_view line = iniText.substr(0, end);
        iniText = end == std::string_view::npos ? std::string_view{} : iniText.substr(end + 1);
        const auto eq = line.find('=');
        if (eq == std::string_view::npos || trim(line.substr(0, eq)) != key) continue;
        const int code = key_config::virtualKeyOf(trim(line.substr(eq + 1)));
        if (code) return code;
    }
    return fallback;
}

}  // namespace

namespace key_config {

int virtualKeyOf(std::string_view name) {
    if (!name.starts_with(kNamePrefix)) return 0;
    name.remove_prefix(kNamePrefix.size());
    if (name.size() == 1 && ((name[0] >= 'A' && name[0] <= 'Z') || (name[0] >= '0' && name[0] <= '9'))) return name[0];
    for (const NamedKey& key : kNamedKeys) {
        if (key.name == name) return key.code;
    }
    return 0;
}

CommandKeys parse(std::string_view iniText) {
    return {keyOrDefault(iniText, kChangeKey, 'V'), keyOrDefault(iniText, kTraceKey, 'E')};
}

CommandKeys load() {
    char* localAppData = nullptr;
    size_t length = 0;
    std::string text;
    if (_dupenv_s(&localAppData, &length, "LOCALAPPDATA") == 0 && localAppData) {
        std::ifstream file(std::string(localAppData) + kIniRelativePath);
        std::stringstream contents;
        contents << file.rdbuf();
        text = contents.str();
    }
    std::free(localAppData);
    return parse(text);
}

}  // namespace key_config
