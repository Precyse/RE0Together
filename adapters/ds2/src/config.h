#pragma once
#include <cstdint>

struct Config {
    uint16_t port = 27980;     // the launcher's loopback port (launcher/games/ds2.json)
    bool overlay = true;       // DX12 hooks and the peer markers
    bool selfMarker = false;   // also mark the local player (checks the projection against the game's own view)
    bool mirrorAnimation = false;  // loopback test: the partner body copies the local player animation
    bool logFacts = false;         // log every FactDatabase write (mapping which facts an action changes)
    bool enemySync = false;    // enemies: the host reports them, a guest tames its own and the host's reports drive them
    bool testCommands = false;  // command files in the coop folder for live checks (teleport, area, weapon, BT region)
    bool cutsceneLog = false;   // log every cutscene (Sequence) start and game-state change
    bool weaponSync = false;   // weapons: the partner's body holds and fires the weapon the partner has drawn
    uint8_t weaponAttachMode = 1;  // SetParent mode of the body's weapon (1 = the engine's own), to try other attach variants live
    bool remoteBody = true;    // the partner's body: a second player entity that walks and rides (adapter.ini remote_body=0 turns it off)
};

// Reads <game dir>\coop\adapter.ini, writing the defaults first when it is missing.
Config loadConfig();
