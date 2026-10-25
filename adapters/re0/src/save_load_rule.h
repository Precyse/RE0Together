#pragma once
#include <cstdint>
#include <optional>
#include <string>

// Which save slot a load request really loads (docs/RE0_NOTES.md "Forced save load"). The launcher's host options
// load_slot / load_hash name the slot the host picked before the launch; whichever slot a player confirms in the
// game's own load list, the session loads that one. Pure, no game access, unit tested.
namespace save_load_rule {

constexpr int32_t kPlayerSlots = 20;  // slots 0..19 are player saves; higher ones are system data (options)
constexpr int32_t kUnknownSlot = -1;

constexpr bool isPlayerSlot(int32_t slot) { return slot >= 0 && slot < kPlayerSlots; }

// The launcher's order. Inactive (slot unknown) means the game's own load menu decides.
struct Order {
    int32_t slot = kUnknownSlot;
    std::optional<uint32_t> hash;  // FNV-1a 32 of that slot's 0x1c850 data bytes; absent: the slot is trusted unchecked

    constexpr bool active() const { return isPlayerSlot(slot); }
};

struct Parsed {
    Order order;
    bool valid = true;  // false: a key held something unusable, the order is off
};

namespace detail {

constexpr int kHexBase = 16;
constexpr int kDecimalBase = 10;
constexpr size_t kMaxHashDigits = 8;
constexpr size_t kMaxSlotDigits = 2;

constexpr int digitValue(char c, int base) {
    int value = -1;
    if (c >= '0' && c <= '9') value = c - '0';
    else if (c >= 'a' && c <= 'f') value = c - 'a' + kDecimalBase;
    else if (c >= 'A' && c <= 'F') value = c - 'A' + kDecimalBase;
    return value >= 0 && value < base ? value : -1;
}

// All of `text` as an unsigned number in `base` (at most `maxDigits` digits), or nothing.
inline std::optional<uint32_t> number(const std::string& text, int base, size_t maxDigits) {
    if (text.empty() || text.size() > maxDigits) return std::nullopt;
    uint32_t value = 0;
    for (const char c : text) {
        const int digit = digitValue(c, base);
        if (digit < 0) return std::nullopt;
        value = value * static_cast<uint32_t>(base) + static_cast<uint32_t>(digit);
    }
    return value;
}

}  // namespace detail

// load_slot: empty = no order; 0..19 = the order; anything else = invalid. load_hash: empty = unchecked; up to 8 hex
// digits; anything else = invalid.
inline Parsed parse(const std::string& slotText, const std::string& hashText) {
    Parsed parsed;
    if (slotText.empty()) return parsed;
    const std::optional<uint32_t> slot = detail::number(slotText, detail::kDecimalBase, detail::kMaxSlotDigits);
    if (!slot || !isPlayerSlot(static_cast<int32_t>(*slot))) return {Order{}, false};
    if (!hashText.empty()) {
        parsed.order.hash = detail::number(hashText, detail::kHexBase, detail::kMaxHashDigits);
        if (!parsed.order.hash) return {Order{}, false};
    }
    parsed.order.slot = static_cast<int32_t>(*slot);
    return parsed;
}

enum class Reason : uint8_t {
    None,         // the request stays as the player made it
    HostSlot,     // a guest always loads the slot the host plays
    SessionSlot,  // the session already plays a slot (loaded or saved): later loads return to it
    Order,        // the first load of the session: the launcher's pick
};

struct Pick {
    int32_t slot;
    Reason reason;
};

// `guest`: this game plays the host's session. `sessionSlot`: the slot the session plays, or kUnknownSlot before its first
// load or save. Only player slots are redirected; the system data the game loads at boot passes.
constexpr Pick pick(const Order& order, bool guest, int32_t requested, int32_t sessionSlot) {
    if (!isPlayerSlot(requested)) return {requested, Reason::None};
    const bool sessionKnown = isPlayerSlot(sessionSlot);
    if (guest && sessionKnown) return {sessionSlot, sessionSlot == requested ? Reason::None : Reason::HostSlot};
    if (!order.active()) return {requested, Reason::None};
    const int32_t target = sessionKnown ? sessionSlot : order.slot;
    if (target == requested) return {requested, Reason::None};
    return {target, sessionKnown ? Reason::SessionSlot : Reason::Order};
}

// The first load of a session must be the bytes the launcher chose: the target slot's in-memory block (what the boot
// read filled) hashes to load_hash. A block that cannot be read, or hashes otherwise, never gets the redirect.
constexpr bool hashAccepts(const Order& order, std::optional<uint32_t> actual) {
    return !order.hash || (actual && *actual == *order.hash);
}

}  // namespace save_load_rule
