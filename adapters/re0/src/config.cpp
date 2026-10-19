#include "config.h"

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>

#include "appearance_rule.h"
#include "log.h"
#include "paths.h"

namespace {

constexpr int kHexBase = 16;
constexpr int kDecimalBase = 10;

const char* const kDefaultIni = "port=27960\ntrace=0\ntrace_vtables=\ncoop=0\noverlay=1\n";

std::string trim(const std::string& s) {
    const auto begin = s.find_first_not_of(" \t\r");
    if (begin == std::string::npos) return "";
    return s.substr(begin, s.find_last_not_of(" \t\r") - begin + 1);
}

std::vector<VtableTrace> parseVtables(const std::string& list) {
    std::vector<VtableTrace> out;
    std::stringstream items(list);
    std::string item;
    while (std::getline(items, item, ',')) {
        const auto colon = item.find(':');
        if (colon == std::string::npos) continue;
        const uintptr_t address = std::strtoul(trim(item.substr(0, colon)).c_str(), nullptr, kHexBase);
        const unsigned count = std::strtoul(trim(item.substr(colon + 1)).c_str(), nullptr, kHexBase);
        if (address != 0 && count != 0) out.push_back({address, count});
    }
    return out;
}

void parsePartnerMode(const std::string& value, Config& config) {
    if (value == partnerModeName(PartnerMode::Input)) {
        config.partnerMode = PartnerMode::Input;
    } else if (value == partnerModeName(PartnerMode::State)) {
        config.partnerMode = PartnerMode::State;
    } else {
        logger::write("config: partner_mode=%s ignored (input or state), keeping input", value.c_str());
    }
}

void parseCharacter(const std::string& value, Config& config) {
    if (value == "rebecca") {
        config.hostCharacter = character_owner::Character::Rebecca;
    } else if (value == "billy") {
        config.hostCharacter = character_owner::Character::Billy;
    } else {
        logger::write("config: character=%s ignored (rebecca or billy)", value.c_str());
    }
}

// An outfit key holds a hex model byte offered for that character; anything else is ignored.
void parseOutfit(const char* key, character_owner::Character character, const std::string& value, Config& config) {
    if (value.empty()) return;
    const std::optional<uint8_t> model = appearance_rule::parseModel(value);
    if (!model || !appearance_rule::find(character, *model)) {
        logger::write("config: %s=%s ignored (not an outfit of that character)", key, value.c_str());
        return;
    }
    config.outfit[static_cast<size_t>(character)] = *model;
}

}  // namespace

const char* partnerModeName(PartnerMode mode) { return mode == PartnerMode::State ? "state" : "input"; }

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
        } else if (key == "trace") {
            config.trace = value == "1";
        } else if (key == "coop") {
            config.coop = value == "1";
        } else if (key == "overlay") {
            config.overlay = value != "0";
        } else if (key == "net_trace") {
            config.netTrace = value == "1";
        } else if (key == "auto_join") {
            config.autoJoin = value == "1";
        } else if (key == "partner_mode") {
            parsePartnerMode(value, config);
        } else if (key == "character") {
            parseCharacter(value, config);
        } else if (key == "outfit_rebecca") {
            parseOutfit("outfit_rebecca", character_owner::Character::Rebecca, value, config);
        } else if (key == "outfit_billy") {
            parseOutfit("outfit_billy", character_owner::Character::Billy, value, config);
        } else if (key == "trace_vtables") {
            config.traceVtables = parseVtables(value);
        }
    }
    logger::write("config: port=%u trace=%d vtables=%zu coop=%d overlay=%d net_trace=%d auto_join=%d", config.port, config.trace,
                  config.traceVtables.size(), config.coop, config.overlay, config.netTrace, config.autoJoin);
    logger::write("config: partner_mode=%s", partnerModeName(config.partnerMode));
    logger::write("config: character=%s outfit_rebecca=%02x outfit_billy=%02x", character_owner::name(config.hostCharacter),
                  config.outfit[static_cast<size_t>(character_owner::Character::Rebecca)],
                  config.outfit[static_cast<size_t>(character_owner::Character::Billy)]);
    return config;
}
