#pragma once
// FACT_SNAPSHOT: the host's whole set of changed facts, chunked, sent to a guest that just joined, reached gameplay or
// asked for a resync, so the FACT_SET deltas are not the only source of the host's world. Each chunk is a
// ChunkHeader followed by a FACT_SET payload (fact_wire.h) of up to fact_wire::kMaxEntries facts. Chunks are reliable
// and ordered with the deltas around them, and writing a fact twice is harmless, so a chunk is applied as it arrives.
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

#include "fact_wire.h"
#include "protocol.h"

namespace fact_snapshot {

constexpr uint16_t kMsgFactSnapshot = proto::kFirstGameType + 0x17;  // 0x0117, host to one guest (or all), reliable
constexpr uint16_t kMaxChunks = 256;  // 65536 facts

struct ChunkHeader {
    uint32_t snapshotId;  // the host's counter, the same for every chunk of one snapshot
    uint16_t index;       // 0-based
    uint16_t count;       // chunks in this snapshot, at least 1
};
static_assert(sizeof(ChunkHeader) == 8);

// The chunk payloads of a snapshot; always at least one, so an empty snapshot still arrives. Empty when there are more
// facts than kMaxChunks chunks hold.
inline std::vector<std::vector<uint8_t>> encode(uint32_t snapshotId, std::span<const fact_wire::Entry> facts) {
    const size_t chunks = std::max<size_t>(1, (facts.size() + fact_wire::kMaxEntries - 1) / fact_wire::kMaxEntries);
    std::vector<std::vector<uint8_t>> out;
    if (chunks > kMaxChunks) return out;
    for (size_t index = 0; index < chunks; ++index) {
        const size_t first = index * fact_wire::kMaxEntries;
        const size_t count = std::min<size_t>(fact_wire::kMaxEntries, facts.size() - first);
        const ChunkHeader header{snapshotId, static_cast<uint16_t>(index), static_cast<uint16_t>(chunks)};
        const std::vector<uint8_t> body = fact_wire::encode(facts.subspan(first, count));
        std::vector<uint8_t> payload(sizeof(header) + body.size());
        std::memcpy(payload.data(), &header, sizeof(header));
        std::memcpy(payload.data() + sizeof(header), body.data(), body.size());
        out.push_back(std::move(payload));
    }
    return out;
}

// False for a payload that is short, names a chunk outside its snapshot, or whose facts do not decode.
inline bool decode(std::span<const uint8_t> payload, ChunkHeader& header, std::vector<fact_wire::Entry>& facts) {
    facts.clear();
    if (payload.size() < sizeof(ChunkHeader)) return false;
    std::memcpy(&header, payload.data(), sizeof(header));
    if (header.count == 0 || header.count > kMaxChunks || header.index >= header.count) return false;
    return fact_wire::decode(payload.subspan(sizeof(header)), facts);
}

// Which chunks of the snapshot being received have arrived.
class Progress {
public:
    enum class State { Partial, Complete, Duplicate };

    // A chunk of a different snapshot starts over (the host sent a newer one, or restarted).
    State onChunk(const ChunkHeader& header) {
        if (header.snapshotId != snapshotId_ || header.count != seen_.size()) {
            snapshotId_ = header.snapshotId;
            seen_.assign(header.count, false);
            seenCount_ = 0;
        }
        if (seen_[header.index]) return State::Duplicate;
        seen_[header.index] = true;
        return ++seenCount_ == seen_.size() ? State::Complete : State::Partial;
    }

private:
    uint32_t snapshotId_ = 0;
    std::vector<bool> seen_;
    size_t seenCount_ = 0;
};

}  // namespace fact_snapshot
