#pragma once
#include <cstdint>

struct Config {
    uint16_t port = 27980;     // the launcher's loopback port (launcher/games/ds2.json)
    bool overlay = true;       // DX12 hooks and the peer markers
    bool selfMarker = false;   // also mark the local player (checks the projection against the game's own view)
    bool remoteBody = false;   // the partner's body: a second player entity that walks and rides
};

// Reads <game dir>\coop\adapter.ini, writing the defaults first when it is missing.
Config loadConfig();
