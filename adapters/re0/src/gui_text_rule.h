#pragma once
#include <array>
#include <cstdint>

// The text overrides of the game's single text lookup (gui_text.cpp), as a pure table (unit tested): which (message
// table slot, message id) is answered with our own string instead of the game's. Ids we add sit above the table's own
// count, so the game never asks for them itself; a relabel reuses a game id.
namespace gui_text_rule {

struct Entry {
    int slot;
    uint32_t id;
    const char* text;  // UTF-8, static
};

// No entries today: the title's Co-op entry is a texture label (title_label.h). New menu strings go here.
constexpr std::array<Entry, 0> kEntries{};

// The override for the message, or null when the game's own text stays.
constexpr const char* find(int slot, uint32_t id) {
    for (const Entry& entry : kEntries) {
        if (entry.slot == slot && entry.id == id) return entry.text;
    }
    return nullptr;
}

}  // namespace gui_text_rule
