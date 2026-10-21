#pragma once
#include <array>
#include <cstdint>

// The text overrides of the game's single text lookup (gui_text.cpp), as a pure table (unit tested): which (message
// table slot, message id) is answered with our own string instead of the game's. Ids we add sit above the table's own
// count, so the game never asks for them itself; a relabel reuses a game id.
namespace gui_text_rule {

constexpr int kOptionTableSlot = 5;  // "option": the help lines of the Options screen

// The help line of the Co-op item in the Options top menu (options_coop.cpp points the item's help entry at it).
constexpr uint32_t kCoopHelpId = 0x1000;

struct Entry {
    int slot;
    uint32_t id;
    const char* text;  // UTF-8, static
};

constexpr std::array kEntries = std::to_array<Entry>({
    {kOptionTableSlot, kCoopHelpId, "Co-op"},
});

// The override for the message, or null when the game's own text stays.
constexpr const char* find(int slot, uint32_t id) {
    for (const Entry& entry : kEntries) {
        if (entry.slot == slot && entry.id == id) return entry.text;
    }
    return nullptr;
}

}  // namespace gui_text_rule
