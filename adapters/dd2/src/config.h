#pragma once
#include <cstdint>
#include <string>

struct Config {
    uint16_t port = 27985;  // the launcher's loopback port (launcher/games/dd2.json)
    bool coop = false;        // the launcher started a co-op session (coop=1)
    bool guest = false;       // ... as a guest (role=guest): the host's world in coop\session, the own character spliced in
    bool echoPuppet = false;  // probe P4-1: a copy of the own Arisen through the stray-pawn route (src/echo_puppet.h); test only
    bool peerPuppet = true;   // each peer's Arisen and main pawn as characters here (src/peer_puppet.h); peer_puppet=0 off
    bool enemySync = true;    // the host's enemies on the guests (src/enemy_stream.h, src/enemy_mirror.h); co-op only, enemy_sync=0 off
    std::string campaign;     // a host's campaign id: its world in coop\campaigns\<id> (src/save_folders.h); empty: its own save
    std::string arisen;       // a guest's character: the campaign id whose character it brings; empty: its own save's
    int loadSlot = -1;        // a guest's forced save slot, the host's (load_slot=<n>; src/load_slot_rule.h); -1: none
    bool autoLoad = false;    // at the idle title menu the game's own Continue is pressed once (src/title_menu.h): on for a co-op
                              // guest with a forced slot, auto_load=0|1 overrides
    int captureSlot = -1;     // a capture launch (capture=<slot>): read that save slot's character at the title (src/offline_capture.h)
};

// Reads <game dir>\coop\adapter.ini, writing the defaults first when it is missing.
Config loadConfig();
