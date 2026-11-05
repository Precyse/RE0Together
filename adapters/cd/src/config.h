#pragma once
#include <cstdint>

struct Config {
    uint16_t port = 27990;  // the launcher's loopback port (launcher/games/cd.json)
    bool capture = false;   // write every funnel packet to coop\capture-<stamp>.cdcap (phase 1 probe L01; tools/cd/capture_dump.py)
    bool devCommands = false;  // coop\cmd.txt commands (marker); never on in a shipped config
    bool presence = true;      // show the other players' copies (injected Acks); presence=0 keeps this machine sniff-only
    bool storyReplay = false;  // a guest posts the host's stage branches (story.h); off until a live session checked the guard
};

// Reads <game dir>\coop\adapter.ini, writing the defaults first when it is missing.
Config loadConfig();
