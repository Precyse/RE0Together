#pragma once
// Which slot a game load really gets (pure, unit tested). A co-op guest plays the host's world: the launcher writes the host's
// slot to adapter.ini (`load_slot`), and any game load the guest asks for (Continue, Load Game, any row) loads that slot. A host,
// a solo game and a guest without a slot load what they asked for; the system data load (-1) is not a game load and is never
// changed.

namespace load_slot_rule {

constexpr int kNoSlot = -1;  // `load_slot` absent or empty: nothing is forced

// Parses adapter.ini's `load_slot` value: a plain number of 0 or more, else kNoSlot.
inline int parse(const char* text) {
    if (!text || !*text) return kNoSlot;
    long value = 0;
    for (const char* c = text; *c; ++c) {
        if (*c < '0' || *c > '9' || value > 1000000) return kNoSlot;
        value = value * 10 + (*c - '0');
    }
    return static_cast<int>(value);
}

// The slot to load when the game asked for `asked`.
inline int slotFor(bool guest, int forced, int asked) {
    return guest && forced >= 0 && asked >= 0 ? forced : asked;
}

}  // namespace load_slot_rule
