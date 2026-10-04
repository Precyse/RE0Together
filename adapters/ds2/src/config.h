#pragma once
#include <cstdint>

struct Config {
    uint16_t port = 27980;     // the launcher's loopback port (launcher/games/ds2.json)
    bool overlay = true;       // DX12 hooks and the peer markers
    bool selfMarker = false;   // also mark the local player (checks the projection against the game's own view)
    bool mirrorAnimation = false;  // loopback test: the partner body copies the local player animation
    bool logFacts = false;         // log every FactDatabase write (mapping which facts an action changes)
    bool enemySync = false;    // enemies: the host reports them, a guest tames its own and the host's reports drive them
    bool remoteBody = true;    // the partner's body: a second player entity that walks and rides (adapter.ini remote_body=0 turns it off)
};

// Reads <game dir>\coop\adapter.ini, writing the defaults first when it is missing.
Config loadConfig();
