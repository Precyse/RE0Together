#include "config.h"

#include <windows.h>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include "game_port_rules.h"
#include "paths.h"
#include "sr2_addresses.h"

namespace {

constexpr int kDecimalBase = 10;
constexpr const char* kTransportKey = "transport";
constexpr const char* kRetiredExperimentalKey = "partner_experimental";
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
        const std::string value = trim(line.substr(eq + 1));
        if (key == kTransportKey) {
            if (const auto transport = parseTransport(value)) {
                config.transport = *transport;
            } else if (!value.empty()) {
                config.rejectedTransport = value;
            }
            continue;
        }
        if (key == kRetiredExperimentalKey) {
            config.retiredPartnerExperimental = true;
            continue;
        }
        unsigned long number = 0;
        if (!parseNumber(value, number)) continue;
        if (key == "max_players") {
            config.maxPlayers = static_cast<uint32_t>(
                std::clamp<unsigned long>(number, sr2::kCoopDefaultPlayers, sr2::kMaxReplicatedPlayers));
        } else if (key == "trace") {
            config.trace = number != 0;
        } else if (key == "game_port") {
            if (game_port_rules::valid(number)) {
                config.gamePort = static_cast<uint32_t>(number);
            } else {
                config.rejectedGamePort = static_cast<uint32_t>(number);
            }
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
