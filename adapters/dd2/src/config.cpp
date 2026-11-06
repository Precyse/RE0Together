#include "config.h"

#include <cstdlib>
#include <fstream>
#include <optional>
#include <string>

#include "load_slot_rule.h"
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
    std::optional<bool> autoLoad;
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
        if (key == "load_slot") config.loadSlot = load_slot_rule::parse(value.c_str());
        if (key == "auto_load" && (value == "0" || value == "1")) autoLoad = value == "1";
        if (key == "capture") config.captureSlot = std::atoi(value.c_str());
    }
    config.autoLoad = autoLoad.value_or(config.coop && config.guest && config.loadSlot >= 0);
    logger::write("config: port=%u coop=%d guest=%d echo_puppet=%d peer_puppet=%d enemy_sync=%d campaign=%s arisen=%s load_slot=%d auto_load=%d",
                  config.port, config.coop, config.guest, config.echoPuppet, config.peerPuppet, config.enemySync,
                  config.campaign.c_str(), config.arisen.c_str(), config.loadSlot, config.autoLoad);
    return config;
}
