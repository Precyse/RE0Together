#pragma once
// The stage and sequencer packets the story module reads (pure, unit tested; coop-scratch\cd\design\static_s14_s16.md,
// build 0x6aeb7d59). Offsets are payload offsets. [V] verified in the handler code, [I] inferred and checked live by
// the counters story.cpp logs.
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <vector>

namespace stage_packets {

constexpr uint8_t kModeApply = 4;  // the Acks' mode byte: anything else only looks the stage up [V]

// StageBranchStateReq (C2S, 14 bytes): u64 instance, u32 branch, u16 flags [V size, I order from the old build].
struct BranchReq {
    uint64_t instance = 0;
    uint32_t branch = 0;
    uint16_t flags = 0;
};
constexpr size_t kReqSize = 14;

// StageBranchStateAck (S2C, 21 + 8n bytes): u8 mode, u32 player, u64 instance at +5 [V]; u32 at +13 and +17 [V], read
// as branch and state [I: FailAck's branch sits at +13].
struct BranchAck {
    uint8_t mode = 0;
    uint64_t instance = 0;
    uint32_t branch = 0, state = 0;
};
constexpr size_t kAckMinSize = 21;
constexpr size_t kAckPairSize = 8;

// PrepareSequencerAck (S2C, at least 23 bytes): u8 mode, u32 player [V]; u64 instance at +9 and u32 sequencer key at +19
// [I: inside a struct the code reads through a virtualised reader].
struct Prepare {
    uint64_t instance = 0;
    uint32_t sequencerKey = 0;
};
constexpr size_t kPrepareMinSize = 23;

// DeleteSequencerAck (S2C, 17 bytes): u32 player, u64 instance at +4, u32 at +12, u8 at +16 [V].
constexpr size_t kDeleteSize = 17;

namespace detail {
template <typename T>
T at(std::span<const uint8_t> payload, size_t offset) {
    T out;
    std::memcpy(&out, payload.data() + offset, sizeof(T));
    return out;
}
}  // namespace detail

inline std::optional<BranchReq> parseReq(std::span<const uint8_t> payload) {
    if (payload.size() != kReqSize) return std::nullopt;
    return BranchReq{detail::at<uint64_t>(payload, 0), detail::at<uint32_t>(payload, 8), detail::at<uint16_t>(payload, 12)};
}

// Our own StageBranchStateReq payload (a replayed branch).
inline std::vector<uint8_t> encodeReq(const BranchReq& req) {
    std::vector<uint8_t> out(kReqSize);
    std::memcpy(out.data(), &req.instance, sizeof(req.instance));
    std::memcpy(out.data() + 8, &req.branch, sizeof(req.branch));
    std::memcpy(out.data() + 12, &req.flags, sizeof(req.flags));
    return out;
}

inline std::optional<BranchAck> parseAck(std::span<const uint8_t> payload) {
    if (payload.size() < kAckMinSize || (payload.size() - kAckMinSize) % kAckPairSize != 0) return std::nullopt;
    return BranchAck{payload[0], detail::at<uint64_t>(payload, 5), detail::at<uint32_t>(payload, 13), detail::at<uint32_t>(payload, 17)};
}

inline std::optional<Prepare> parsePrepare(std::span<const uint8_t> payload) {
    if (payload.size() < kPrepareMinSize) return std::nullopt;
    return Prepare{detail::at<uint64_t>(payload, 9), detail::at<uint32_t>(payload, 19)};
}

// The instance a DeleteSequencerAck removes.
inline std::optional<uint64_t> parseDelete(std::span<const uint8_t> payload) {
    if (payload.size() != kDeleteSize) return std::nullopt;
    return detail::at<uint64_t>(payload, 4);
}

}  // namespace stage_packets
