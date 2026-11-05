#include "config.h"

#include <cstdlib>
#include <fstream>
#include <string>

#include "log.h"
#include "paths.h"

namespace {

constexpr int kDecimalBase = 10;

const char* const kDefaultIni = "port=27990\ncapture=0\npresence=1\n";

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
    {"capture", &Config::capture},
    {"dev_commands", &Config::devCommands},
    {"presence", &Config::presence},
    {"story_replay", &Config::storyReplay},
};

void apply(Config& config, const std::string& key, const std::string& value) {
    for (const Flag& flag : kFlags) {
        if (key != flag.key) continue;
        if (value == "1" || value == "0") config.*flag.field = value == "1";
        return;
    }
    if (key == "port") config.port = static_cast<uint16_t>(std::strtoul(value.c_str(), nullptr, kDecimalBase));
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
        apply(config, trim(line.substr(0, eq)), trim(line.substr(eq + 1)));
    }
    logger::write("config: port=%u capture=%d dev_commands=%d presence=%d story_replay=%d", config.port, config.capture,
                  config.devCommands, config.presence, config.storyReplay);
    return config;
}
