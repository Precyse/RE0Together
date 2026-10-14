#pragma once
#include <cstdint>
#include <iterator>
#include <vector>

#include "code_patch.h"

// The code edits of milestone 5g, L1, as data (pure, unit tested): the five places that ask for the host name's address
// list. Each is a gethostname / gethostbyname pair; the call to gethostbyname goes through the stub at 0xC23544. The
// addresses every peer reports and uses (the game's own record, the second local IP getter, GameSpy's localip list,
// GameSpy 0x8D7BC0 and the net library 0xC1C0C0) must all come from one list, so all five are retargeted together or none.
namespace lan_address_patches {

constexpr uintptr_t kHostByNameStub = 0xC23544;  // jmp [gethostbyname]; stdcall(name) returns hostent*

inline constexpr uintptr_t kHostByNameCalls[] = {0x810C94, 0x7F79D0, 0x8E1B3E, 0x8D7C00, 0xC1C0FE};

// All five calls going to `thunk`, or none when `maxPlayers` is the shipped co-op's.
inline std::vector<CodePatch> forPlayers(uint32_t maxPlayers, uintptr_t thunk) {
    if (maxPlayers <= 2) return {};
    std::vector<CodePatch> patches;
    for (const uintptr_t address : kHostByNameCalls) {
        patches.push_back(retargetedCall("host_by_name", address, kHostByNameStub, thunk));
    }
    return patches;
}

}  // namespace lan_address_patches
