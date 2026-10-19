#pragma once
#include <cstdint>
#include <string>

#include "transport.h"

struct Config {
    uint32_t maxPlayers = 4;
    bool trace = true;
    uint32_t gamePort = 4200;          // the UDP port of the game socket; the shipped game binds 4200
    uint32_t rejectedGamePort = 0;     // a game_port value outside 1024..65535 that was ignored, 0 = none
    Transport transport = Transport::Native;
    std::string rejectedTransport;     // a transport value that is neither native nor steam, empty = none
    bool retiredPartnerExperimental = false;  // the ini has partner_experimental, a key that no longer does anything
};

// Reads key=value lines (max_players, trace, game_port, transport); unknown keys and bad values keep the defaults,
// max_players is clamped, a game_port outside 1024..65535 is ignored and kept in rejectedGamePort, a transport other than
// native or steam is ignored and kept in rejectedTransport, the retired partner_experimental key is ignored and flagged.
Config parseConfig(const std::string& text);

// Reads <game dir>\coop\sr2_adapter.ini, writing the defaults first when it is missing.
Config loadConfig();
