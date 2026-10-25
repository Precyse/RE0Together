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
#include "option_values.h"
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

// world_pause=never (default): with a peer the inventory, the map and the player's own messages keep the world
// running (live menus); menus: every screen pauses the world as the game does.
using option_values::kWorldPauseMenus;
using option_values::kWorldPauseNever;

const char* worldPauseName(bool liveMenus) { return (liveMenus ? kWorldPauseNever : kWorldPauseMenus).data(); }

void parseWorldPause(const std::string& value, Config& config) {
    if (value == kWorldPauseNever) {
        config.liveMenus = true;
    } else if (value == kWorldPauseMenus) {
        config.liveMenus = false;
    } else {
        logger::write("config: world_pause=%s ignored (never or menus), keeping never", value.c_str());
    }
}

void parseCharacter(const std::string& value, Config& config) {
    if (const auto character = option_values::characterOf(value)) {
        config.hostCharacter = *character;
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

void parseLoadOrder(const std::string& slot, const std::string& hash, Config& config) {
    const save_load_rule::Parsed parsed = save_load_rule::parse(slot, hash);
    if (!parsed.valid) {
        logger::write("config: load_slot=%s load_hash=%s ignored (slot 0 to 19, hash up to 8 hex digits), no forced load",
                      slot.c_str(), hash.c_str());
        return;
    }
    config.loadOrder = parsed.order;
    if (!parsed.order.active()) return;
    if (parsed.order.hash) logger::write("config: load_slot=%d load_hash=%08x", parsed.order.slot, *parsed.order.hash);
    else logger::write("config: load_slot=%d without load_hash, the slot is trusted unchecked", parsed.order.slot);
}

}  // namespace

const char* partnerModeName(PartnerMode mode) { return mode == PartnerMode::State ? "state" : "input"; }

Config loadConfig() {
    const std::wstring path = coopDirectory() + L"\\adapter.ini";
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) std::ofstream(path) << kDefaultIni;

    Config config;
    std::string loadSlot;
    std::string loadHash;
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
        } else if (key == "dev_commands") {
            config.devCommands = value == "1";
        } else if (key == option_values::kWorldPauseKey) {
            parseWorldPause(value, config);
        } else if (key == option_values::kPartnerModeKey) {
            parsePartnerMode(value, config);
        } else if (key == option_values::kCharacterKey) {
            parseCharacter(value, config);
        } else if (key == option_values::kOutfitRebeccaKey) {
            parseOutfit(key.c_str(), character_owner::Character::Rebecca, value, config);
        } else if (key == option_values::kOutfitBillyKey) {
            parseOutfit(key.c_str(), character_owner::Character::Billy, value, config);
        } else if (key == option_values::kLoadSlotKey) {
            loadSlot = value;
        } else if (key == "load_hash") {
            loadHash = value;
        } else if (key == "trace_vtables") {
            config.traceVtables = parseVtables(value);
        }
    }
    parseLoadOrder(loadSlot, loadHash, config);
    logger::write("config: port=%u trace=%d vtables=%zu coop=%d overlay=%d net_trace=%d auto_join=%d", config.port, config.trace,
                  config.traceVtables.size(), config.coop, config.overlay, config.netTrace, config.autoJoin);
    logger::write("config: partner_mode=%s world_pause=%s dev_commands=%d", partnerModeName(config.partnerMode),
                  worldPauseName(config.liveMenus), config.devCommands);
    logger::write("config: character=%s outfit_rebecca=%02x outfit_billy=%02x", character_owner::name(config.hostCharacter),
                  config.outfit[static_cast<size_t>(character_owner::Character::Rebecca)],
                  config.outfit[static_cast<size_t>(character_owner::Character::Billy)]);
    return config;
}
