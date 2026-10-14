#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

// The pure rules of the member leave path (milestone 5i, section 8; unit tested): when a member leaves, the game's
// 2-player code lets every machine take over its objects and rename the ids it created into the machine's own handle space,
// which with 3 or more players makes the machines disagree. Here the machine that owns a record decides its new id and
// reissues it (destroy the old id, queue the creation), and a guest only drops the leaver's records locally.
namespace leave_rules {

constexpr uint8_t kDestroyMessage = 0x2A;  // {old id}: remove the record with that id
constexpr size_t kNarrowIdOffset = 3;      // the id in a message with a 3 byte header
constexpr size_t kWideIdOffset = 4;        // and with a 4 byte header (the byte [0xE8C30C] says which)
constexpr size_t kHeaderBytes = 3;
constexpr uint32_t kLastUnusableState = 7;  // a member is sent to once its state is above this

// A message ready for the game's send: the bytes, and how many of them.
struct Message {
    std::array<uint8_t, 8> bytes{};
    size_t length = 0;
};

// The destroy message of a record, as the game's own destroy (0x804710) builds it: id 0x2A, the payload length at +1, the id
// at +3 (+4 with the wide header).
inline Message destroyMessage(uint16_t oldId, bool wideHeader) {
    Message message;
    const size_t idOffset = wideHeader ? kWideIdOffset : kNarrowIdOffset;
    message.length = idOffset + sizeof(oldId);
    message.bytes[0] = kDestroyMessage;
    const uint16_t payload = static_cast<uint16_t>(message.length - kHeaderBytes);
    message.bytes[1] = static_cast<uint8_t>(payload);
    message.bytes[2] = static_cast<uint8_t>(payload >> 8);
    message.bytes[idOffset] = static_cast<uint8_t>(oldId);
    message.bytes[idOffset + 1] = static_cast<uint8_t>(oldId >> 8);
    return message;
}

// A member that gets a reissued record: not us, not the leaver, able to receive, and past connecting.
inline bool usableMember(bool local, uint8_t sendEnabled, uint32_t state, uint8_t slot, uint8_t leaverSlot) {
    return !local && sendEnabled != 0 && state > kLastUnusableState && slot != leaverSlot;
}

// Whether the rename of a record whose id is from the leaver's slot happens on this machine: for the records it owns, and on
// the host also for the leaver's own that nobody took over, as the 2-player game did.
inline bool keepsRename(uint8_t owner, uint8_t localSlot, uint8_t leaverSlot, bool localIsHost) {
    return owner == localSlot || (localIsHost && owner == leaverSlot);
}

// A guest drops a record of the leaver locally (the host's messages replace it); the host, and any machine when the
// leaver is the host (the session is lost anyway), keep the game's own handling.
inline bool guestDrops(bool localIsHost, uint8_t leaverSlot, uint8_t hostSlot) {
    return !localIsHost && leaverSlot != hostSlot;
}

// A record to reissue and the id the other machines know it by.
struct Item {
    uintptr_t record;
    uint16_t oldId;
};

// The records to reissue after a leave, once each.
class ReissueSet {
public:
    void add(uintptr_t record, uint16_t oldId) {
        const bool known = std::any_of(items_.begin(), items_.end(), [&](const Item& item) { return item.record == record; });
        if (!known) items_.push_back({record, oldId});
    }

    const std::vector<Item>& items() const { return items_; }

private:
    std::vector<Item> items_;
};

}  // namespace leave_rules
