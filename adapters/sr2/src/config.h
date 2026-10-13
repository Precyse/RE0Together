#pragma once
#include <cstdint>
#include <string>

struct Config {
    uint32_t maxPlayers = 4;
    bool trace = true;
    bool partnerExperimental = false;  // also patch the sites that wait for the live probes B and G
};

// Reads key=value lines (max_players, trace, partner_experimental); unknown keys and bad values keep the defaults,
// max_players is clamped.
Config parseConfig(const std::string& text);

// Reads <game dir>\coop\sr2_adapter.ini, writing the defaults first when it is missing.
Config loadConfig();
