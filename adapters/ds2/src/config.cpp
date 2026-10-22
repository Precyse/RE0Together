#include "config.h"

#include <windows.h>

#include <cstdlib>
#include <fstream>
#include <string>

#include "log.h"
#include "paths.h"

namespace {

constexpr int kDecimalBase = 10;

const char* const kDefaultIni = "port=27980\noverlay=1\nself_marker=0\n";

std::string trim(const std::string& s) {
    const auto begin = s.find_first_not_of(" \t\r");
    if (begin == std::string::npos) return "";
    return s.substr(begin, s.find_last_not_of(" \t\r") - begin + 1);
}

// The on/off settings: "1" turns one on, "0" off, anything else leaves its default (config.h).
struct Flag {
    const char* key;
    bool Config::*field;
};
constexpr Flag kFlags[] = {
    {"overlay", &Config::overlay},
    {"self_marker", &Config::selfMarker},
    {"mirror_animation", &Config::mirrorAnimation},
    {"enemy_sync", &Config::enemySync},
    {"log_facts", &Config::logFacts},
    {"diagnostics", &Config::diagnostics},
    {"orders_diagnostics", &Config::ordersDiagnostics},
    {"test_commands", &Config::testCommands},
    {"god_mode", &Config::godMode},
    {"cutscene_log", &Config::cutsceneLog},
    {"cutscene_sync", &Config::cutsceneSync},
    {"weapon_sync", &Config::weaponSync},
    {"gear_restore", &Config::gearRestore},
    {"remote_body", &Config::remoteBody},
};

unsigned long number(const std::string& value) { return std::strtoul(value.c_str(), nullptr, kDecimalBase); }

void apply(Config& config, const std::string& key, const std::string& value) {
    for (const Flag& flag : kFlags) {
        if (key != flag.key) continue;
        if (value == "1" || value == "0") config.*flag.field = value == "1";
        return;
    }
    if (key == "port") {
        config.port = static_cast<uint16_t>(number(value));
    } else if (key == "cutscene_share_min_frames") {
        config.cutsceneShareMinFrames = static_cast<uint32_t>(number(value));
    } else if (key == "weapon_attach_mode") {
        config.weaponAttachMode = static_cast<uint8_t>(number(value));
    }
}

}  // namespace

Config loadConfig() {
    const std::wstring path = coopDirectory() + L"\\adapter.ini";
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) std::ofstream(path) << kDefaultIni;

    Config config;
    std::ifstream file(path);
    std::string line;
    while (std::getline(file, line)) {
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        apply(config, trim(line.substr(0, eq)), trim(line.substr(eq + 1)));
    }
    logger::write("config: port=%u overlay=%d self_marker=%d remote_body=%d", config.port, config.overlay,
                  config.selfMarker, config.remoteBody);
    return config;
}
