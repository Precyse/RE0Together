#pragma once
// A player's character template and how a receiver turns it into a copy of that player (pure, unit tested; cd design
// players.md 1-2, STUDY.md 2.4). A template is two ActorLoadDataAck payloads the sender captured of its own player:
// the user packet (user record at +0x03, the owned player key at +0x0E, character number at +0x17 and +0x43, world
// position 154 bytes and a yaw quaternion 142 bytes before the end) and the body packet (its own world-class record,
// key at +0x03, then a byte copy of the user packet).
//
// The receiver: puts the copy's keys everywhere the sender's keys stand (the method cdmp shipped on two PCs), its own
// character number (the client keeps one equipment cache per number), the current position, cuts the body to its own
// record (a duplicate player record gives "Actor creation failed"), and refuses a result that still names the sender's
// keys or names any key of the receiver's own world.
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "actor_key.h"

namespace template_rule {

using Bytes = std::vector<uint8_t>;

constexpr size_t kUserKey = 0x03;
constexpr size_t kOwnedKey = 0x0E;
constexpr size_t kBodyKey = 0x03;
constexpr std::array<size_t, 2> kCharacterNumber{0x17, 0x43};
constexpr size_t kPositionFromEnd = 154;
constexpr size_t kRotationFromEnd = 142;
constexpr size_t kUserCopyProbe = 64;  // bytes of the user packet looked for inside the body packet
constexpr size_t kMinUser = kPositionFromEnd + 1;
constexpr size_t kMaxLoad = 0x7FC0;    // a load must fit one packet

// Slot blocks of copy keys (adapter.md 9): outside the server's 15,501-index pool, one block of 0x10 per lobby slot.
constexpr uint32_t kCopyUserBase = 0x9010F001;
constexpr uint32_t kCopyPlayerBase = 0xA010F001;
constexpr uint32_t kCopyBodyBase = 0xB010F001;
constexpr uint64_t kCopyCharacterNumberBase = 0x7FFFFFFFFFFFF001ull;
constexpr uint32_t kSlotStride = 0x10;

struct Keys {
    uint32_t user = 0, player = 0, body = 0;
};

inline Keys copyKeys(uint8_t slot) {
    return {kCopyUserBase + kSlotStride * slot, kCopyPlayerBase + kSlotStride * slot, kCopyBodyBase + kSlotStride * slot};
}

inline uint64_t copyCharacterNumber(uint8_t slot) { return kCopyCharacterNumberBase + kSlotStride * slot; }

// Any key we invented for a copy (user, player, body, horse of any slot): its index sits above the server's pool.
inline bool isCopyKey(uint32_t key) {
    return actor_key::valid(key) && actor_key::indexOf(key) >= actor_key::indexOf(kCopyUserBase);
}

// The lobby slot whose copy has this player key, or null for any other key.
inline std::optional<uint8_t> copySlotOfPlayer(uint32_t key) {
    constexpr uint32_t kSlots = 0x100;
    if (key < kCopyPlayerBase || (key - kCopyPlayerBase) % kSlotStride != 0) return std::nullopt;
    const uint32_t slot = (key - kCopyPlayerBase) / kSlotStride;
    if (slot >= kSlots) return std::nullopt;
    return static_cast<uint8_t>(slot);
}

template <typename T>
T read(std::span<const uint8_t> bytes, size_t at) {
    T value{};
    std::memcpy(&value, bytes.data() + at, sizeof(T));
    return value;
}

// The keys a template names (zero keys when the packets are too short or the classes are wrong).
inline Keys senderKeys(std::span<const uint8_t> user, std::span<const uint8_t> body) {
    if (user.size() < kMinUser || body.size() < kBodyKey + 4) return {};
    const Keys keys{read<uint32_t>(user, kUserKey), read<uint32_t>(user, kOwnedKey), read<uint32_t>(body, kBodyKey)};
    if (actor_key::classOf(keys.user) != actor_key::Class::User || actor_key::classOf(keys.player) != actor_key::Class::Player ||
        actor_key::classOf(keys.body) != actor_key::Class::World) {
        return {};
    }
    return keys;
}

inline size_t countKey(std::span<const uint8_t> bytes, uint32_t key) {
    size_t n = 0;
    for (size_t at = 0; at + 4 <= bytes.size(); ++at) n += read<uint32_t>(bytes, at) == key;
    return n;
}

inline size_t replaceKey(Bytes& bytes, uint32_t from, uint32_t to) {
    size_t n = 0;
    for (size_t at = 0; at + 4 <= bytes.size(); ++at) {
        if (read<uint32_t>(bytes, at) != from) continue;
        std::memcpy(bytes.data() + at, &to, sizeof(to));
        at += 3;
        ++n;
    }
    return n;
}

// The body packet without its copy of the user packet; null when the copy is not found after the body's own record.
inline std::optional<Bytes> truncateBody(std::span<const uint8_t> body, std::span<const uint8_t> user) {
    if (user.size() < kUserCopyProbe) return std::nullopt;
    for (size_t at = kBodyKey + 4; at + kUserCopyProbe <= body.size(); ++at) {
        if (std::memcmp(body.data() + at, user.data(), kUserCopyProbe) == 0) return Bytes(body.begin(), body.begin() + at);
    }
    return std::nullopt;
}

struct Copy {
    Bytes user;
    Bytes body;
};

struct Placement {
    float position[3] = {};
    float rotation[4] = {0, 0, 0, 1};
};

// The copy of a sender's template for the receiver, or the reason it is refused (`why`).
inline std::optional<Copy> makeCopy(std::span<const uint8_t> user, std::span<const uint8_t> body, uint8_t senderSlot,
                                    const Placement& at, std::initializer_list<uint32_t> receiverKeys, std::string& why) {
    if (user.size() < kMinUser || user.size() > kMaxLoad || body.size() > kMaxLoad) {
        why = "size";
        return std::nullopt;
    }
    const Keys sender = senderKeys(user, body);
    if (!sender.user) {
        why = "key classes";
        return std::nullopt;
    }
    const uint64_t number = read<uint64_t>(user, kCharacterNumber[0]);
    if (!number || read<uint64_t>(user, kCharacterNumber[1]) != number) {
        why = "character number";
        return std::nullopt;
    }
    for (float v : at.position) {
        if (!std::isfinite(v)) {
            why = "position";
            return std::nullopt;
        }
    }
    auto truncated = truncateBody(body, user);
    if (!truncated) {
        why = "body has no user copy";
        return std::nullopt;
    }
    Copy copy{Bytes(user.begin(), user.end()), std::move(*truncated)};
    const Keys target = copyKeys(senderSlot);
    for (Bytes* packet : {&copy.user, &copy.body}) {
        replaceKey(*packet, sender.user, target.user);
        replaceKey(*packet, sender.player, target.player);
        replaceKey(*packet, sender.body, target.body);
    }
    const uint64_t copyNumber = copyCharacterNumber(senderSlot);
    for (size_t offset : kCharacterNumber) std::memcpy(copy.user.data() + offset, &copyNumber, sizeof(copyNumber));
    std::memcpy(copy.user.data() + copy.user.size() - kPositionFromEnd, at.position, sizeof(at.position));
    std::memcpy(copy.user.data() + copy.user.size() - kRotationFromEnd, at.rotation, sizeof(at.rotation));
    for (const Bytes* packet : {&copy.user, &copy.body}) {
        for (uint32_t key : receiverKeys) {
            if (key && countKey(*packet, key)) {
                why = "names a key of this world";
                return std::nullopt;
            }
        }
    }
    return copy;
}

}  // namespace template_rule
