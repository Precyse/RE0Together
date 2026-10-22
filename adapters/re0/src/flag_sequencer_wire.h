#pragma once
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <vector>

#include "flag_diff.h"
#include "protocol.h"

// Wire format of the host-ordered story flags (partner_mode=state, docs/RE0_NOTES.md "State mode: flags"). Pure, no
// game access, unit tested.
namespace flag_sequencer_wire {

constexpr uint16_t kMsgFlagPropose = proto::kFirstGameType + 0x75;  // 0x0175, guest to host, reliable
constexpr uint16_t kMsgFlagCommit = proto::kFirstGameType + 0x76;   // 0x0176, host to all, reliable

// FLAG_PROPOSE: the flag_sync diff the guest made natively, stamped with the world epoch the guest last adopted
// (world_epoch_rule.h) so the host can drop a proposal from before its last load.
struct ProposeHeader {
    uint16_t count;
    uint16_t worldEpoch;
};
static_assert(sizeof(ProposeHeader) == 4);

struct Propose {
    uint16_t worldEpoch = 0;
    std::vector<flag_diff::Change> changes;
};

// FLAG_COMMIT: the host's order. `origin` is the slot whose change this is (the host's own, or the proposing guest,
// which counts it as the answer to its oldest proposal). An empty commit answers a proposal that changed nothing.
struct CommitHeader {
    uint32_t serial;
    uint8_t origin;
    uint8_t reserved;
    uint16_t count;
};
static_assert(sizeof(CommitHeader) == 8);

struct Commit {
    uint32_t serial = 0;
    uint8_t origin = 0;
    std::vector<flag_diff::Change> changes;
};

namespace detail {

inline bool validChange(const flag_diff::Change& change) {
    return change.word < flag_diff::kWords && (change.set & change.clear) == 0;
}

inline std::optional<std::vector<flag_diff::Change>> readChanges(std::span<const uint8_t> bytes, uint16_t count) {
    if (count > flag_diff::kWords || bytes.size() != count * sizeof(flag_diff::Change)) return std::nullopt;
    std::vector<flag_diff::Change> changes(count);
    if (count) std::memcpy(changes.data(), bytes.data(), bytes.size());
    for (const flag_diff::Change& change : changes) {
        if (!validChange(change)) return std::nullopt;
    }
    return changes;
}

inline void appendChanges(std::vector<uint8_t>& out, const std::vector<flag_diff::Change>& changes) {
    const size_t at = out.size();
    out.resize(at + changes.size() * sizeof(flag_diff::Change));
    if (!changes.empty()) std::memcpy(out.data() + at, changes.data(), changes.size() * sizeof(flag_diff::Change));
}

}  // namespace detail

inline std::vector<uint8_t> encodePropose(const Propose& propose) {
    const ProposeHeader header{static_cast<uint16_t>(propose.changes.size()), propose.worldEpoch};
    std::vector<uint8_t> out(sizeof(header));
    std::memcpy(out.data(), &header, sizeof(header));
    detail::appendChanges(out, propose.changes);
    return out;
}

inline std::optional<Propose> decodePropose(std::span<const uint8_t> bytes) {
    ProposeHeader header{};
    if (bytes.size() < sizeof(header)) return std::nullopt;
    std::memcpy(&header, bytes.data(), sizeof(header));
    auto changes = detail::readChanges(bytes.subspan(sizeof(header)), header.count);
    if (!changes) return std::nullopt;
    return Propose{header.worldEpoch, std::move(*changes)};
}

inline std::vector<uint8_t> encodeCommit(const Commit& commit) {
    const CommitHeader header{commit.serial, commit.origin, 0, static_cast<uint16_t>(commit.changes.size())};
    std::vector<uint8_t> out(sizeof(header));
    std::memcpy(out.data(), &header, sizeof(header));
    detail::appendChanges(out, commit.changes);
    return out;
}

inline std::optional<Commit> decodeCommit(std::span<const uint8_t> bytes) {
    CommitHeader header{};
    if (bytes.size() < sizeof(header)) return std::nullopt;
    std::memcpy(&header, bytes.data(), sizeof(header));
    auto changes = detail::readChanges(bytes.subspan(sizeof(header)), header.count);
    if (!changes) return std::nullopt;
    return Commit{header.serial, header.origin, std::move(*changes)};
}

}  // namespace flag_sequencer_wire
