#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

#include "sr2_addresses.h"

// The pure rules of the anim set traces (milestone 14 AN1 to AN3, unit tested): the need table text, the anim headers that cannot
// be decoded, and which chunk streamed anim slot an address is in.
namespace anim_trace_rules {

// One row of the need table 0x2229528: the anim group and whether this machine and the remote need it. The game keeps one remote
// byte for all remotes.
struct NeedRow {
    uint16_t group;
    bool local;
    bool remote;
};

// "{0x02BF 1 0, 0x02C5 1 1}": the rows somebody needs, as group, local, remote.
inline std::string needTableText(const NeedRow* rows, size_t count) {
    std::string text = "{";
    bool first = true;
    for (size_t i = 0; i < count; ++i) {
        if (!rows[i].local && !rows[i].remote) continue;
        char part[32];
        std::snprintf(part, sizeof(part), "%s0x%04X %d %d", first ? "" : ", ", rows[i].group, rows[i].local ? 1 : 0, rows[i].remote ? 1 : 0);
        text += part;
        first = false;
    }
    return text + "}";
}

// The decode (0xCE4870) reads the header offset at +0x2C of an anim's data when flag 0x40 is set in the byte at +5, and trusts it. A
// header offset at or past the size of the anim cannot be right.
constexpr bool undecodable(uint32_t data, uint8_t headerFlags, uint32_t headerOffset, uint32_t size) {
    return data != 0 && (headerFlags & sr2::kAnimHeaderOffsetFlag) != 0 && headerOffset >= size;
}

// The chunk anim slot (0..2) whose pool [base, base + size) holds `address`, -1 for none.
struct Slot {
    uint32_t base;
    uint32_t size;
};

constexpr int slotOf(uint32_t address, const Slot* slots, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        if (address >= slots[i].base && address - slots[i].base < slots[i].size) return static_cast<int>(i);
    }
    return -1;
}

}  // namespace anim_trace_rules
