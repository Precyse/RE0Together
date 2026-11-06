#pragma once
// Refuses to hook a CrimsonDesert.exe other than the one cd_sites.h was taken from: a game update moves every address
// at once, and a hook on the wrong bytes crashes the game. Two layers: the PE identity, then the bytes of every site.
#include <cstdint>

#include "cd_sites.h"

namespace build_gate {

struct Identity {
    uint32_t timeDateStamp = 0;
    uint64_t imageBase = 0;
};

constexpr Identity kSupported{0x6AEB7D59, 0x140000000};

// Reads the identity out of a PE image's headers (the loaded exe, or a copy of its first page); zeros when the headers
// are not a PE32+ image.
Identity identityOf(const void* image, uint32_t availableBytes);

bool supported(const Identity& found);

// The running exe's identity (and its actual load address) against kSupported; logs the result.
bool checkRunningGame();

// False (and logged as `conflict: cdmp present: co-op off`) while another co-op mod's file is beside the exe; the launcher
// moves it away at install, and its log reader names it (launcher/ModLoadCheck.cs).
bool checkNoConflict();

// True when `live` starts with the site's expected bytes.
bool siteMatches(const cd_sites::Site& site, const uint8_t* live);

// Every row of `set` matches the running game; logs each row that does not.
bool setIntact(const char* set);

}  // namespace build_gate
