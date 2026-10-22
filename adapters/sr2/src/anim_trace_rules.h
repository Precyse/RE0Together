#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>
#include <string>
#include <vector>

#include "sr2_addresses.h"

// The pure rules of the anim set traces (milestone 14 AN1 to AN3, milestone 16 W1 AN4; unit tested): the need table text and where
// the game's code keeps the table, the anim headers that cannot be decoded, which chunk streamed anim slot an address is in and which
// other sets share a slot.
namespace anim_trace_rules {

// One row of the need table: the anim group, whether this machine needs it and the mask of the remote members that need it (one bit
// per member slot, anim_stream_rules::slotBit; 1 in the shipped table).
struct NeedRow {
    uint16_t group;
    bool local;
    uint8_t mask;
};

// "{0x02BF 1 0x00, 0x02C5 0 0x06}": the rows somebody needs, as group, local, mask.
inline std::string needTableText(const NeedRow* rows, size_t count) {
    std::string text = "{";
    bool first = true;
    for (size_t i = 0; i < count; ++i) {
        if (!rows[i].local && rows[i].mask == 0) continue;
        char part[32];
        std::snprintf(part, sizeof(part), "%s0x%04X %d 0x%02X", first ? "" : ", ", rows[i].group, rows[i].local ? 1 : 0, rows[i].mask);
        text += part;
        first = false;
    }
    return text + "}";
}

// " id 0x00001234" for a streaming_sync request or answer (the payload names the id of the set), "" for the other subs.
inline std::string syncIdText(uint8_t sub, uint32_t id) {
    if (sub > sr2::kStreamingSubAnswer) return "";
    char text[24];
    std::snprintf(text, sizeof(text), " id 0x%08X", id);
    return text;
}

// Where the game's code says the need table is: the base and the number of rows.
struct NeedTableView {
    uint32_t base;
    size_t rows;
};

// The table from the two immediates of the game's own code (the base at 0xA824E1 and the end at 0xA824F4), so that it is found
// wherever the table is. A pair that is not a whole number of rows gives none.
constexpr NeedTableView needTableFromCode(uint32_t baseImm, uint32_t endImm) {
    if (endImm <= baseImm || (endImm - baseImm) % sr2::kAnimNeedRowBytes != 0) return {0, 0};
    return {baseImm, (endImm - baseImm) / sr2::kAnimNeedRowBytes};
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

// A stream entry as the other-sets list reads it: its state, the slot descriptor of its object and the object's group.
struct StreamEntry {
    uint32_t state;
    uint32_t descriptor;
    uint16_t group;
};

// The entry states the game takes as free (0xA82EA0).
constexpr bool entryInUse(uint32_t state) { return state != 0 && state != 3 && state != 4; }

// The groups other than `group` that entries in use hold in the slot descriptor `descriptor`, in entry order. A slot holds one set,
// so a non-empty list is a bystander or a duplicate load.
inline std::vector<uint16_t> otherSetsInSlot(std::span<const StreamEntry> entries, uint32_t descriptor, uint16_t group) {
    std::vector<uint16_t> others;
    for (const StreamEntry& entry : entries) {
        if (entryInUse(entry.state) && entry.descriptor == descriptor && entry.group != group) others.push_back(entry.group);
    }
    return others;
}

// "{0x02C5, 0x02C6}", "{}" for none.
inline std::string groupsText(const std::vector<uint16_t>& groups) {
    std::string text = "{";
    for (size_t i = 0; i < groups.size(); ++i) {
        char part[16];
        std::snprintf(part, sizeof(part), "%s0x%04X", i == 0 ? "" : ", ", groups[i]);
        text += part;
    }
    return text + "}";
}

}  // namespace anim_trace_rules
