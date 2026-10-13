#pragma once
// The shared locker (docs/DS2_NOTES.md, "Shared locker"): a locker is a construction object's baggage slot of kind 28
// (Public) or 29 (PublicReceived), named by the owner's identifier (a hash of the construction id, the same in both worlds)
// and the slot kind. The host's world is the authority: it lists the contents of every locker (LOCKER_LIST), a guest reports
// the move its own game made (LOCKER_MOVE) and the host answers whether it stands (LOCKER_RESULT). All reliable.
#include <cmath>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

#include "cargo_transfer.h"
#include "protocol.h"

namespace locker_wire {

constexpr uint16_t kMsgLockerList = proto::kFirstGameType + 0x34;    // 0x0134, host to all or to a joining peer: LockerHeader, CargoAdd[count]
constexpr uint16_t kMsgLockerMove = proto::kFirstGameType + 0x35;    // 0x0135, guest to host: LockerMove
constexpr uint16_t kMsgLockerResult = proto::kFirstGameType + 0x36;  // 0x0136, host to the requesting guest: LockerResult

constexpr uint16_t kMaxLockerPieces = 128;
constexpr uint8_t kFlagSnapshotEnd = 1;  // key 0, kind 0, count 0: the lockers not named since the snapshot began are empty
constexpr uint8_t kDirectionPut = 1;     // the guest's backpack to the locker
constexpr uint8_t kDirectionTake = 2;    // the locker to the guest's backpack

enum class Reason : uint8_t {
    Ok,
    UnknownLocker,
    Full,
    NotThere,
    OrderHeld,
    Malformed,
    Rate,
    Count
};

#pragma pack(push, 1)
struct LockerHeader {
    uint32_t key;     // the owner's identifier; 0 with kFlagSnapshotEnd
    uint8_t slotKind; // game::kSlotPublic or game::kSlotPublicReceived
    uint8_t flags;
    uint16_t count;   // pieces that follow, at most kMaxLockerPieces
    uint32_t replyTo; // the LOCKER_MOVE request id this list answers, 0 = none
    uint32_t reserved;
};
static_assert(sizeof(LockerHeader) == 16);

struct LockerMove {
    uint32_t requestId;  // per guest, from 1
    uint32_t key;
    uint8_t slotKind;
    uint8_t direction;
    uint16_t reserved;
    uint32_t reserved2;
    uint64_t guestHandle;  // the piece in the guest's world now (for its undo); the host ignores it
    cargo_transfer::CargoAdd piece;
};
static_assert(sizeof(LockerMove) == 56);

struct LockerResult {
    uint32_t requestId;
    uint8_t accepted;  // 1 done, 0 refused
    uint8_t reason;    // Reason; Ok exactly when accepted
    uint16_t reserved;
};
static_assert(sizeof(LockerResult) == 8);
#pragma pack(pop)

inline bool validKind(uint8_t kind) { return kind == game::kSlotPublic || kind == game::kSlotPublicReceived; }

inline bool plausible(const cargo_transfer::CargoAdd& piece) {
    return std::isfinite(piece.durability) && piece.reserved[0] == 0 && piece.reserved[1] == 0 && piece.reserved[2] == 0 &&
           piece.reserved2 == 0;
}

// The payload of one locker's list: `header.count` is set from `pieces` (at most kMaxLockerPieces).
inline std::vector<uint8_t> encodeList(LockerHeader header, std::span<const cargo_transfer::CargoAdd> pieces) {
    header.count = static_cast<uint16_t>(pieces.size());
    std::vector<uint8_t> out(sizeof(header) + pieces.size_bytes());
    std::memcpy(out.data(), &header, sizeof(header));
    if (!pieces.empty()) std::memcpy(out.data() + sizeof(header), pieces.data(), pieces.size_bytes());
    return out;
}

// False for a size that is not the header plus `count` pieces, a count over kMaxLockerPieces, a kind that is not a shared
// locker's (the snapshot end has kind, key and count 0), flags with unknown bits, a non-zero reserved field or a piece that
// is not plausible (nothing is returned).
inline bool decodeList(std::span<const uint8_t> payload, LockerHeader& header, std::vector<cargo_transfer::CargoAdd>& pieces) {
    LockerHeader read;
    if (payload.size() < sizeof(read)) return false;
    std::memcpy(&read, payload.data(), sizeof(read));
    if (read.count > kMaxLockerPieces || read.reserved != 0 || (read.flags & ~kFlagSnapshotEnd) != 0) return false;
    if (payload.size() != sizeof(read) + read.count * sizeof(cargo_transfer::CargoAdd)) return false;
    const bool end = (read.flags & kFlagSnapshotEnd) != 0;
    if (end ? (read.key != 0 || read.slotKind != 0 || read.count != 0 || read.replyTo != 0) : !validKind(read.slotKind)) return false;
    std::vector<cargo_transfer::CargoAdd> list(read.count);
    if (read.count) std::memcpy(list.data(), payload.data() + sizeof(read), read.count * sizeof(cargo_transfer::CargoAdd));
    for (const cargo_transfer::CargoAdd& piece : list) {
        if (!plausible(piece)) return false;
    }
    header = read;
    pieces = std::move(list);
    return true;
}

inline bool decodeMove(std::span<const uint8_t> payload, LockerMove& out) {
    LockerMove move;
    if (payload.size() != sizeof(move)) return false;
    std::memcpy(&move, payload.data(), sizeof(move));
    const bool direction = move.direction == kDirectionPut || move.direction == kDirectionTake;
    if (move.requestId == 0 || !validKind(move.slotKind) || !direction || move.reserved != 0 || move.reserved2 != 0 ||
        !plausible(move.piece)) {
        return false;
    }
    out = move;
    return true;
}

inline bool decodeResult(std::span<const uint8_t> payload, LockerResult& out) {
    LockerResult result;
    if (payload.size() != sizeof(result)) return false;
    std::memcpy(&result, payload.data(), sizeof(result));
    const bool ok = result.accepted == 1 && result.reason == static_cast<uint8_t>(Reason::Ok);
    const bool refused = result.accepted == 0 && result.reason != static_cast<uint8_t>(Reason::Ok) &&
                         result.reason < static_cast<uint8_t>(Reason::Count);
    if (result.requestId == 0 || result.reserved != 0 || !(ok || refused)) return false;
    out = result;
    return true;
}

inline LockerResult resultOf(uint32_t requestId, Reason reason) {
    return {requestId, static_cast<uint8_t>(reason == Reason::Ok ? 1 : 0), static_cast<uint8_t>(reason), 0};
}

}  // namespace locker_wire
