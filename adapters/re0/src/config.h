#pragma once
#include <cstdint>
#include <array>
#include <vector>

#include "character_owner.h"
#include "save_load_rule.h"

struct VtableTrace {
    uintptr_t address;
    unsigned count;
};

// How the remote-owned character is driven: `Input` replays the owner's pad through the game's own think (shipped);
// `State` shows the owner's sampled pose and motion requests on a puppet (experimental, docs/RE0_NOTES.md "Pose puppet").
enum class PartnerMode : uint8_t { Input, State };

const char* partnerModeName(PartnerMode mode);

struct Config {
    uint16_t port = 27960;
    bool coop = false;
    bool trace = false;
    bool overlay = true;  // D3D hooks installed; the panel itself stays hidden until F8
    bool netTrace = false;  // record the partner's pad and state packets to coop/net_trace.bin
    bool autoJoin = false;  // guest: drive the menus into the host's game by itself (off: the guest loads any slot by hand)
    PartnerMode partnerMode = PartnerMode::Input;  // adapter.ini partner_mode=input|state
    bool devCommands = false;  // coop\cmd.txt developer commands for live tests (dev_command.h)
    bool liveMenus = true;  // world_pause=never|menus: never = live menus, the world runs under the player's own screens (state mode only)
    character_owner::Character hostCharacter = character_owner::Character::Rebecca;  // who the host plays, the guest gets the other
    std::array<uint8_t, character_owner::kCharacterCount> outfit{};  // per Character: chosen model byte, kNoOutfit keeps the save's
    save_load_rule::Order loadOrder;  // load_slot / load_hash: the slot the session loads whatever the load list confirms
    std::vector<VtableTrace> traceVtables;
};

// Reads <game dir>\coop\adapter.ini, writing the defaults first when it is missing.
Config loadConfig();
