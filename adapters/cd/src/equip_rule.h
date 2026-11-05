#pragma once
// Equipment of a player copy (pure, unit tested; cd design players.md 4, STUDY.md "Equipment and appearance").
//
// The sender keeps every slot that changed since its template was captured (Loadout), with whether the template had
// something on it. The receiver replays that on the copy with the engine's MMO messages: OtherUnequipItemAck for every
// slot that holds something and must change, then one OtherEquipItemAck with only puts. The client drops a WHOLE
// OtherEquipItemAck when a put lands on an occupied slot, and applies it only when its serial is above the copy's
// last one, so serials rise strictly and never fall below the sender's.
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <map>
#include <optional>
#include <span>
#include <vector>

namespace equip_rule {

constexpr uint16_t kMaxSlot = 63;          // equipment slots seen in the game go up to about 23
constexpr size_t kMaxWriterItems = 32;     // items one writer call may carry
constexpr uint8_t kFragmentMarker = 4;     // first byte of an OtherEquipItemAck payload
constexpr size_t kEquipHeader = 9;         // marker, u32 key, u32 serial
constexpr size_t kEquipEntry = 6;          // u32 item, u16 slot
constexpr size_t kUnequipSize = 10;        // u32 key, u16 slot, u32 serial
constexpr size_t kUnequipItemAckSize = 12; // u32 key, u16 slot, u16 inventory slot, u32 serial

struct Item {
    uint32_t item = 0;  // static item id, the same on every PC; 0 = empty the slot
    uint16_t slot = 0;
};

struct SlotState {
    uint16_t slot = 0;
    uint32_t item = 0;        // on the slot now; 0 = empty
    bool hadAtLoad = false;   // the template had something on it
};

// The sender: changes since the template, one state per slot.
class Loadout {
public:
    // One writer call or server unequip. A swap comes as [{new, s}, {0, s}]: the removal tells that the slot was
    // occupied before. False, and nothing changes, for an implausible change.
    bool apply(uint32_t serial, std::span<const Item> items) {
        if (items.empty() || items.size() > kMaxWriterItems) return false;
        for (const Item& i : items) {
            if (i.slot > kMaxSlot) return false;
        }
        for (const Item& i : items) {
            if (slots_.count(i.slot)) continue;
            const bool removal = std::any_of(items.begin(), items.end(), [&](const Item& o) { return o.slot == i.slot && o.item == 0; });
            slots_[i.slot] = {i.slot, 0, removal};
        }
        for (const Item& i : items) {
            if (i.item == 0) slots_[i.slot].item = 0;
        }
        for (const Item& i : items) {
            if (i.item != 0) slots_[i.slot].item = i.item;
        }
        serial_ = std::max(serial_, serial);
        return true;
    }

    void clear() {
        slots_.clear();
        serial_ = 0;
    }

    std::vector<SlotState> slots() const {
        std::vector<SlotState> out;
        for (const auto& [slot, state] : slots_) out.push_back(state);
        return out;
    }
    uint32_t serial() const { return serial_; }

private:
    std::map<uint16_t, SlotState> slots_;
    uint32_t serial_ = 0;
};

// What the receiver last put on one slot of the copy.
struct CopySlot {
    bool filled = false;
    uint32_t item = 0;  // 0 while filled by the template (item unknown)
};

struct Plan {
    std::vector<uint16_t> unequips;  // first, one message each
    std::vector<Item> puts;          // then one message
    bool empty() const { return unequips.empty() && puts.empty(); }
};

// The messages that bring the copy (`copy`, updated in place; a slot not in it is still as the template left it) to
// the sender's `wanted` slots.
inline Plan plan(std::span<const SlotState> wanted, std::map<uint16_t, CopySlot>& copy) {
    Plan out;
    for (const SlotState& w : wanted) {
        const auto it = copy.find(w.slot);
        const CopySlot now = it != copy.end() ? it->second : CopySlot{w.hadAtLoad, 0};
        if (now.filled && w.item != 0 && now.item == w.item) continue;
        if (!now.filled && w.item == 0) continue;
        if (now.filled) out.unequips.push_back(w.slot);
        if (w.item != 0) out.puts.push_back({w.item, w.slot});
        copy[w.slot] = {w.item != 0, w.item};
    }
    return out;
}

// The serial for the next message to a copy: above the last one sent to it and never below the sender's.
inline uint32_t nextSerial(std::optional<uint32_t> last, uint32_t sender) {
    return last ? std::max(*last + 1, sender) : sender;
}

inline std::vector<uint8_t> otherEquip(uint32_t key, uint32_t serial, std::span<const Item> puts) {
    std::vector<uint8_t> out(kEquipHeader + kEquipEntry * puts.size());
    out[0] = kFragmentMarker;
    std::memcpy(out.data() + 1, &key, sizeof(key));
    std::memcpy(out.data() + 5, &serial, sizeof(serial));
    for (size_t i = 0; i < puts.size(); ++i) {
        std::memcpy(out.data() + kEquipHeader + i * kEquipEntry, &puts[i].item, sizeof(uint32_t));
        std::memcpy(out.data() + kEquipHeader + i * kEquipEntry + sizeof(uint32_t), &puts[i].slot, sizeof(uint16_t));
    }
    return out;
}

inline std::vector<uint8_t> otherUnequip(uint32_t key, uint16_t slot, uint32_t serial) {
    std::vector<uint8_t> out(kUnequipSize);
    std::memcpy(out.data(), &key, sizeof(key));
    std::memcpy(out.data() + 4, &slot, sizeof(slot));
    std::memcpy(out.data() + 6, &serial, sizeof(serial));
    return out;
}

// UnequipItemAck our server sent our client: the unequip of `key`'s slot as a Loadout change.
struct Unequip {
    uint32_t key = 0;
    uint32_t serial = 0;
    Item item;
};
inline std::optional<Unequip> parseUnequipItemAck(std::span<const uint8_t> payload) {
    if (payload.size() != kUnequipItemAckSize) return std::nullopt;
    Unequip out;
    std::memcpy(&out.key, payload.data(), sizeof(out.key));
    std::memcpy(&out.item.slot, payload.data() + 4, sizeof(out.item.slot));
    std::memcpy(&out.serial, payload.data() + 8, sizeof(out.serial));
    if (out.item.slot > kMaxSlot) return std::nullopt;
    return out;
}

}  // namespace equip_rule
