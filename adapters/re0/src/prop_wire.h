#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>

#include "protocol.h"

// Wire format of the push block messages (partner_mode=state, docs/RE0_NOTES.md "State mode: props"). Pure, no game
// access, unit tested.
namespace prop_wire {

constexpr uint16_t kMsgLease = proto::kFirstGameType + 0x72;   // 0x0172, pusher <-> host, reliable
constexpr uint16_t kMsgMove = proto::kFirstGameType + 0x73;    // 0x0173, holder to the other machine, unreliable at 10 Hz (reliable for a push's last pose)
constexpr uint16_t kMsgCommit = proto::kFirstGameType + 0x74;  // 0x0174, host to all, reliable

constexpr uint8_t kObstacleKind = 0x70;  // the registry class of uObstacle (0x80000070)
constexpr uint8_t kUnitCount = 5;        // obstacle units a room script names: rows 0..4 of the table at 0xcd5398
constexpr uint16_t kSceneLimit = 0xaf;   // scene ids are below this (scene::kSceneIdLimit)

enum class LeaseOp : uint8_t { Request, Grant, Deny, Release };
constexpr uint8_t kLastLeaseOp = static_cast<uint8_t>(LeaseOp::Release);

// PROP_LEASE: Request and Release (pusher to host) name the sender as the holder; Grant and Deny (host to all) answer a
// request: `holder` is who holds the box after the answer (the asker for a Grant, the one who has it for a Deny).
// `serial` numbers the host's grant (Grant and Deny); a Request and a Release carry 0.
struct Lease {
    uint32_t serial;
    uint16_t scene;
    uint8_t unitId;
    uint8_t op;  // LeaseOp
    uint8_t holder;
    uint8_t reserved[3];
};
static_assert(sizeof(Lease) == 12);

// PROP_MOVE: the holder's box pose at its own clock `senderMs` (the same clock POSE_STATE carries).
struct Move {
    uint32_t senderMs;
    uint16_t scene;
    uint8_t kind;  // kObstacleKind
    uint8_t unitId;
    float pos[3];
    float yaw;
};
static_assert(sizeof(Move) == 24);

// PROP_COMMIT: the host's record of a finished push. `serial` is the lease's, `committer` the slot that pushed.
struct Commit {
    uint32_t serial;
    uint16_t scene;
    uint8_t kind;
    uint8_t unitId;
    float pos[3];
    float yaw;
    uint8_t committer;
    uint8_t reserved[3];
};
static_assert(sizeof(Commit) == 28);

namespace detail {

template <typename T>
std::optional<T> read(std::span<const uint8_t> bytes) {
    T value{};
    if (bytes.size() != sizeof(value)) return std::nullopt;
    std::memcpy(&value, bytes.data(), sizeof(value));
    return value;
}

inline bool finite(const float (&pos)[3], float yaw) {
    return std::isfinite(pos[0]) && std::isfinite(pos[1]) && std::isfinite(pos[2]) && std::isfinite(yaw);
}

inline bool nameable(uint16_t scene, uint8_t unitId) { return scene < kSceneLimit && unitId < kUnitCount; }

}  // namespace detail

// A received message of the right size that names a room and an obstacle unit the game has, an operation it knows,
// and finite numbers; anything else is nothing.
inline std::optional<Lease> decodeLease(std::span<const uint8_t> bytes) {
    const auto lease = detail::read<Lease>(bytes);
    if (!lease || lease->op > kLastLeaseOp || !detail::nameable(lease->scene, lease->unitId)) return std::nullopt;
    return lease;
}

inline std::optional<Move> decodeMove(std::span<const uint8_t> bytes) {
    const auto move = detail::read<Move>(bytes);
    if (!move || move->kind != kObstacleKind || !detail::nameable(move->scene, move->unitId) ||
        !detail::finite(move->pos, move->yaw)) {
        return std::nullopt;
    }
    return move;
}

inline std::optional<Commit> decodeCommit(std::span<const uint8_t> bytes) {
    const auto commit = detail::read<Commit>(bytes);
    if (!commit || commit->kind != kObstacleKind || !detail::nameable(commit->scene, commit->unitId) ||
        !detail::finite(commit->pos, commit->yaw)) {
        return std::nullopt;
    }
    return commit;
}

}  // namespace prop_wire
