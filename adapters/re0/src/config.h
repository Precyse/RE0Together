#pragma once
#include <cstdint>
#include <array>
#include <vector>

#include "character_owner.h"

struct VtableTrace {
    uintptr_t address;
    unsigned count;
};

struct Config {
    uint16_t port = 27960;
    bool coop = false;
    bool trace = false;
    bool overlay = true;  // D3D hooks installed; the panel itself stays hidden until F8
    bool netTrace = false;  // record the partner's pad and state packets to coop/net_trace.bin
    bool autoJoin = false;  // guest: drive the menus into the host's game by itself (off: the guest loads any slot by hand)
    character_owner::Character hostCharacter = character_owner::Character::Rebecca;  // who the host plays, the guest gets the other
    std::array<uint8_t, character_owner::kCharacterCount> outfit{};  // per Character: chosen model byte, kNoOutfit keeps the save's
    std::vector<VtableTrace> traceVtables;
};

// Reads <game dir>\coop\adapter.ini, writing the defaults first when it is missing.
Config loadConfig();
