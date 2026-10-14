#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

// The pure rules of the member leave path (milestone 5i, section 8 and milestone 9 N2; unit tested): when a member leaves,
// the game's 2-player code lets every machine take over its objects and rename the ids it created into the machine's own
// handle space, which with 3 or more players makes the machines disagree. Here the machine that owns a record decides its
// new id and reissues it (destroy the old id, queue the creation), and a guest only drops the leaver's records locally.
namespace leave_rules {

constexpr uint8_t kDestroyMessage = 0x0F;  // rop_destroy: remove the record with an id (0x2A is rop_new_owner)
constexpr size_t kHeaderBytes = 3;         // message id and payload length; the wide header (byte [0xE8C084]) has one more
constexpr size_t kWideHeaderBytes = 4;
constexpr size_t kMaxMessageBytes = 0x200;  // the game builds it in a 0x500 byte buffer and refuses to send 0x1FF or more
constexpr uint32_t kLastUnusableState = 7;  // a member is sent to once its state is above this

// A message ready for the game's send: the bytes, and how many of them.
struct Message {
    std::array<uint8_t, kMaxMessageBytes> bytes{};
    size_t length = 0;
};

// The start of a record's destroy message, as the game's destroy broadcast 0x8053B0 builds it: id 0x0F, the payload length
// (set by `sealed`), the session's value [session+8] (4 bytes), the record's class byte and the old id. The record's class
// then appends its own payload at `length` (its vtable +0x14).
inline Message destroyMessage(uint32_t sessionValue, uint8_t recordClass, uint16_t oldId, bool wideHeader) {
    Message message;
    size_t at = wideHeader ? kWideHeaderBytes : kHeaderBytes;
    message.bytes[0] = kDestroyMessage;
    for (size_t i = 0; i < sizeof(sessionValue); ++i) message.bytes[at++] = static_cast<uint8_t>(sessionValue >> (8 * i));
    message.bytes[at++] = recordClass;
    message.bytes[at++] = static_cast<uint8_t>(oldId);
    message.bytes[at++] = static_cast<uint8_t>(oldId >> 8);
    message.length = at;
    return message;
}

// The message with its payload length written (the bytes after the 3 byte header).
inline Message sealed(Message message) {
    const uint16_t payload = static_cast<uint16_t>(message.length - kHeaderBytes);
    message.bytes[1] = static_cast<uint8_t>(payload);
    message.bytes[2] = static_cast<uint8_t>(payload >> 8);
    return message;
}

// What a guest does when `leaver` leaves the session. The shipped callback only ended a cutscene (0x6C6B90), which with one
// remote member was always the host. Another guest leaving runs the host's pass over the objects instead (0x8072E0, in the
// session states in which the host's leave handler 0x819880 runs it) and leaves the cutscene alone.
enum class GuestLeave { CutsceneCleanup, ObjectPass, Nothing };

inline GuestLeave guestLeave(uint32_t sessionState, bool leaverIsHost, bool leaverIsLocal) {
    constexpr uint32_t kIdle = 0;
    constexpr uint32_t kPassStates[] = {1, 6, 8};
    if (sessionState == kIdle || leaverIsHost || leaverIsLocal) return GuestLeave::CutsceneCleanup;
    for (const uint32_t state : kPassStates) {
        if (state == sessionState) return GuestLeave::ObjectPass;
    }
    return GuestLeave::Nothing;
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
