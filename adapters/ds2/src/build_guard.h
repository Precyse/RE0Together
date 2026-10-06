#pragma once
// Refuses to hook a DS2.exe other than the one the fixed addresses were taken from (Steam build 23923251): a game
// update moves every address at once, and a hook on the wrong bytes crashes the game.
#include <cstdint>

namespace build_guard {

struct Identity {
    uint32_t timeDateStamp = 0;
    uint32_t sizeOfImage = 0;
};

constexpr Identity kSupported{0x6a3dae46, 0xb292000};

// Reads the identity out of a PE image's headers (the loaded exe, or a copy of its first page); zeros when the headers are not a PE.
Identity identityOf(const void* image, uint32_t availableBytes);

bool supported(const Identity& found);

// The running exe's identity against kSupported; logs the result.
bool checkRunningGame();

}  // namespace build_guard
