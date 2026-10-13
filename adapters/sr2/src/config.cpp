#include "config.h"

#include <windows.h>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include "paths.h"
#include "sr2_addresses.h"

namespace {

constexpr int kDecimalBase = 10;
const char* const kDefaultIni = "max_players=4\ntrace=1\n";

std::string trim(const std::string& s) {
    const auto begin = s.find_first_not_of(" \t\r");
    if (begin == std::string::npos) return "";
    return s.substr(begin, s.find_last_not_of(" \t\r") - begin + 1);
}

bool parseNumber(const std::string& value, unsigned long& out) {
    char* end = nullptr;
    out = std::strtoul(value.c_str(), &end, kDecimalBase);
    return !value.empty() && *end == '\0';
}

}  // namespace

Config parseConfig(const std::string& text) {
    Config config;
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) {
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = trim(line.substr(0, eq));
        unsigned long number = 0;
        if (!parseNumber(trim(line.substr(eq + 1)), number)) continue;
        if (key == "max_players") {
            config.maxPlayers = static_cast<uint32_t>(
                std::clamp<unsigned long>(number, sr2::kCoopDefaultPlayers, sr2::kPoolPlayerSlots));
        } else if (key == "trace") {
            config.trace = number != 0;
        }
    }
    return config;
}

Config loadConfig() {
    const std::wstring path = coopDirectory() + L"\\sr2_adapter.ini";
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) std::ofstream(path) << kDefaultIni;
    std::ifstream file(path);
    std::stringstream text;
    text << file.rdbuf();
    return parseConfig(text.str());
}
