#pragma once
// Req to Ack recipes for the other player's copy (pure, unit tested; cd design adapter.md 7, STUDY.md 2.4, checked by
// size against 39,684 / 23,669 / 12,712 captured Reqs on build 0x6AB28F00). Sizes are payload bytes after the 5-byte
// packet header. The adapter compares them with its own sniffed Reqs before injecting anything, so a game update that
// changes a layout turns presence off instead of feeding the client a wrong size (which it drops silently at best).
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <vector>

namespace presence_rule {

// MoveActor: Req = body (0xDE) + history (10 x 28) + tail (12); Ack = body + one recipient-set byte + tail.
constexpr size_t kMoveReqSize = 514;
constexpr size_t kMoveBody = 0xDE;
constexpr size_t kMoveHistory = 10 * 28;
constexpr size_t kMoveTail = 12;
constexpr size_t kMoveCompactSize = kMoveBody + kMoveTail;  // what CHAR_MOVE carries
constexpr size_t kMoveAckSize = kMoveBody + 1 + kMoveTail;
static_assert(kMoveBody + kMoveHistory + kMoveTail == kMoveReqSize);
static_assert(kMoveAckSize == 235);
constexpr size_t kMovePosition = 0x04;     // f32 x3, world
constexpr size_t kMoveQuaternion = 0x10;   // f32 x4
constexpr size_t kMoveParent = 0x8C;       // u64: the mount, 0 on foot
constexpr size_t kMoveTimestamp = 0xD5;    // u64 ms, same offset in the Ack (before the inserted byte)

// StartAction: same size both ways; the Req's last byte (force flag) moves to Ack +0x8E and block B shifts up one.
constexpr size_t kActionSize = 725;
constexpr size_t kActionHead = 0x8E;
constexpr size_t kActionReqTimestamp = 0x15F;
constexpr size_t kActionAckTimestamp = 0x160;
constexpr size_t kActionReqParent = 0x116;  // first byte of the parent field (Req)
constexpr size_t kActionReqTarget = 0x16C;  // first byte of the target field (Req)
constexpr size_t kActionRefBytes = 4;       // the low word of each reference must be 0 for a relay without key mapping

// RotateActor: Ack = Req with one byte inserted at +0xA4.
constexpr size_t kRotateReqSize = 176;
constexpr size_t kRotateInsertAt = 0xA4;
constexpr size_t kRotateAckSize = kRotateReqSize + 1;
constexpr size_t kRotateTimestamp = 0x9C;

constexpr size_t kKey = 0x00;  // the actor key, u32, in all three

using Bytes = std::vector<uint8_t>;

template <typename T>
T read(std::span<const uint8_t> bytes, size_t at) {
    T value{};
    std::memcpy(&value, bytes.data() + at, sizeof(T));
    return value;
}

template <typename T>
void write(Bytes& bytes, size_t at, T value) {
    std::memcpy(bytes.data() + at, &value, sizeof(T));
}

// A Move Req without its history (what the sender puts on the wire).
inline std::optional<Bytes> compactMove(std::span<const uint8_t> req) {
    if (req.size() != kMoveReqSize) return std::nullopt;
    Bytes out(req.begin(), req.begin() + kMoveBody);
    out.insert(out.end(), req.end() - kMoveTail, req.end());
    return out;
}

// The MoveActorAck the receiver injects: the copy's key, on foot (parent 0), restamped.
inline std::optional<Bytes> moveAck(std::span<const uint8_t> compact, uint32_t copyKey, uint64_t stampMs) {
    if (compact.size() != kMoveCompactSize) return std::nullopt;
    Bytes out(compact.begin(), compact.begin() + kMoveBody);
    out.push_back(0);  // recipient set
    out.insert(out.end(), compact.begin() + kMoveBody, compact.end());
    write<uint32_t>(out, kKey, copyKey);
    write<uint64_t>(out, kMoveParent, 0);
    write<uint64_t>(out, kMoveTimestamp, stampMs);
    return out;
}

// True when a StartAction Req names no other actor (parent and target words are 0), so it can be relayed without
// translating keys; one that does is held back and counted until the key map exists.
inline bool actionIsSelfContained(std::span<const uint8_t> req) {
    if (req.size() != kActionSize) return false;
    return read<uint32_t>(req, kActionReqParent) == 0 && read<uint32_t>(req, kActionReqTarget) == 0;
}

inline std::optional<Bytes> actionAck(std::span<const uint8_t> req, uint32_t copyKey, uint64_t stampMs) {
    if (req.size() != kActionSize) return std::nullopt;
    Bytes out(req.begin(), req.begin() + kActionHead);
    out.push_back(req.back());  // force flag
    out.insert(out.end(), req.begin() + kActionHead, req.end() - 1);
    write<uint32_t>(out, kKey, copyKey);
    write<uint64_t>(out, kActionAckTimestamp, stampMs);
    return out;
}

inline std::optional<Bytes> rotateAck(std::span<const uint8_t> req, uint32_t copyKey, uint64_t stampMs) {
    if (req.size() != kRotateReqSize) return std::nullopt;
    Bytes out(req.begin(), req.begin() + kRotateInsertAt);
    out.push_back(0);
    out.insert(out.end(), req.begin() + kRotateInsertAt, req.end());
    write<uint32_t>(out, kKey, copyKey);
    write<uint64_t>(out, kRotateTimestamp, stampMs);
    return out;
}

// The timestamp a Req carries (for the clock anchor and restamping).
inline std::optional<uint64_t> moveStamp(std::span<const uint8_t> compactOrReq) {
    if (compactOrReq.size() != kMoveCompactSize && compactOrReq.size() != kMoveReqSize) return std::nullopt;
    return read<uint64_t>(compactOrReq, kMoveTimestamp);
}

// A RemoveActorAck for one key: u8 4, u32 0, u8 reason, u32 key (10 bytes, the form cdmp shipped).
constexpr uint8_t kRemoveMarker = 4;
constexpr uint8_t kRemoveFade = 0x15;  // the game's own fade-out
inline Bytes removeAck(uint32_t key, uint8_t reason = kRemoveFade) {
    Bytes out(10, 0);
    out[0] = kRemoveMarker;
    out[5] = reason;
    write<uint32_t>(out, 6, key);
    return out;
}

// A whole packet: u16 opcode, u8 slot 0, u16 payload length, payload.
inline Bytes packetOf(uint16_t opcode, std::span<const uint8_t> payload) {
    Bytes out(5 + payload.size());
    write<uint16_t>(out, 0, opcode);
    write<uint16_t>(out, 3, static_cast<uint16_t>(payload.size()));
    std::memcpy(out.data() + 5, payload.data(), payload.size());
    return out;
}

}  // namespace presence_rule
