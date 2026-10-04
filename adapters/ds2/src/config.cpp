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
        const std::string key = trim(line.substr(0, eq));
        const std::string value = trim(line.substr(eq + 1));
        if (key == "port") {
            config.port = static_cast<uint16_t>(std::strtoul(value.c_str(), nullptr, kDecimalBase));
        } else if (key == "overlay") {
            config.overlay = value != "0";
        } else if (key == "self_marker") {
            config.selfMarker = value == "1";
        } else if (key == "mirror_animation") {
            config.mirrorAnimation = value == "1";
        } else if (key == "enemy_sync") {
            config.enemySync = value == "1";
        } else if (key == "log_facts") {
            config.logFacts = value == "1";
        } else if (key == "remote_body") {
            config.remoteBody = value == "1";
        }
    }
    logger::write("config: port=%u overlay=%d self_marker=%d remote_body=%d", config.port, config.overlay,
                  config.selfMarker, config.remoteBody);
    return config;
}
