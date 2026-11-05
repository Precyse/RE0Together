#pragma once
#include <cstdint>

struct Config {
    uint16_t port = 27985;  // the launcher's loopback port (launcher/games/dd2.json)
    bool coop = false;        // the launcher started a co-op session (coop=1)
    bool guest = false;       // ... as a guest (role=guest): the host's world in coop\session, the own character spliced in
    bool echoPuppet = false;  // probe P4-1: a copy of the own Arisen through the stray-pawn route (src/echo_puppet.h); test only
    bool peerPuppet = true;   // each peer's Arisen and main pawn as characters here (src/peer_puppet.h); peer_puppet=0 off
    bool enemySync = true;    // the host's enemies on the guests (src/enemy_stream.h, src/enemy_mirror.h); co-op only, enemy_sync=0 off
};

// Reads <game dir>\coop\adapter.ini, writing the defaults first when it is missing.
Config loadConfig();
