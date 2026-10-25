#pragma once
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>

#include "protocol.h"

// Wire format of the world digest canary (partner_mode=state, docs/RE0_NOTES.md "State mode: world digest"). Pure, no
// game access, unit tested.
namespace world_digest_wire {

constexpr uint16_t kMsgWorldDigest = proto::kFirstGameType + 0x78;  // 0x0178, every machine to all, unreliable at 1 Hz

constexpr uint8_t kCaughtUp = 1;  // flags: the sender holds the world (a guest after its join handshake; the host always)
constexpr uint8_t kKnownFlags = kCaughtUp;

// WORLD_DIGEST: what the sender's replicated stores hold, small enough to compare at a glance. The three flag CRCs leave
// out the presence flags (each machine's own); `flagSerial` is the sender's last flag commit, the point in the host's
// order the CRCs describe. Two digests are only comparable at the same serial with no proposal in flight.
struct WorldDigest {
    uint32_t flagSerial;
    uint32_t flagCrc;        // every flag family but enemy and act
    uint32_t enemyFlagCrc;   // flag family 0x312..0x411
    uint32_t actFlagCrc;     // flag family 0x612..0x711
    uint32_t authorityHash;  // room authority table: (scene, owner, epoch) of every owned room
    uint16_t floorJournal;   // floor changes since the last save or load
    uint16_t loadSerial;
    uint8_t pendingProposals;
    uint8_t flags;
    uint16_t scene;
    uint32_t senderMs;
};
static_assert(sizeof(WorldDigest) == 32);

constexpr bool caughtUp(const WorldDigest& digest) { return (digest.flags & kCaughtUp) != 0; }

// A received WORLD_DIGEST of exactly the right size and no flag bit this build does not know, or nothing.
inline std::optional<WorldDigest> decode(std::span<const uint8_t> bytes) {
    WorldDigest digest{};
    if (bytes.size() != sizeof(digest)) return std::nullopt;
    std::memcpy(&digest, bytes.data(), sizeof(digest));
    if (digest.flags & ~kKnownFlags) return std::nullopt;
    return digest;
}

}  // namespace world_digest_wire
