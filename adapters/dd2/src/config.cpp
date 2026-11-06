#include "config.h"

#include <cstdlib>
#include <fstream>
#include <string>

#include "log.h"
#include "save_folders.h"
#include "paths.h"

namespace {

constexpr int kDecimalBase = 10;

const char* const kDefaultIni = "port=27985\n";

std::string trim(const std::string& s) {
    const auto begin = s.find_first_not_of(" \t\r");
    if (begin == std::string::npos) return "";
    return s.substr(begin, s.find_last_not_of(" \t\r") - begin + 1);
}

}  // namespace

Config loadConfig() {
    const std::wstring path = coopDirectory() + L"\\adapter.ini";
    std::ifstream in(path);
    if (!in) {
        std::ofstream(path) << kDefaultIni;
        in.open(path);
    }
    Config config;
    std::string line;
    while (std::getline(in, line)) {
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = trim(line.substr(0, eq)), value = trim(line.substr(eq + 1));
        if (key == "coop" && (value == "0" || value == "1")) config.coop = value == "1";
        if (key == "role") config.guest = value == "guest";
        if (key == "port") config.port = static_cast<uint16_t>(std::strtoul(value.c_str(), nullptr, kDecimalBase));
        if (key == "echo_puppet" && (value == "0" || value == "1")) config.echoPuppet = value == "1";
        if (key == "peer_puppet" && (value == "0" || value == "1")) config.peerPuppet = value == "1";
        if (key == "enemy_sync" && (value == "0" || value == "1")) config.enemySync = value == "1";
        if (key == "campaign" && save_folders::validId(value)) config.campaign = value;
        if (key == "arisen" && save_folders::validId(value)) config.arisen = value;
    }
    logger::write("config: port=%u coop=%d guest=%d echo_puppet=%d peer_puppet=%d enemy_sync=%d campaign=%s arisen=%s", config.port,
                  config.coop, config.guest, config.echoPuppet, config.peerPuppet, config.enemySync, config.campaign.c_str(),
                  config.arisen.c_str());
    return config;
}
