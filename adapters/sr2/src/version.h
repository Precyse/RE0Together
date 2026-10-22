#pragma once
#include <cstddef>
#include <cstdint>

// What identifies this mod build on the wire (docs/SR2_NOTES.md, "Version gate"). Every join_req and join_acc a modded
// peer sends ends with the trailer below; a peer without a matching one is refused.
namespace version {

// The one place that is bumped whenever the wire behaviour of the mod changes.
inline constexpr uint16_t kProtocolVersion = 3;  // 3: milestone 11 B3 (the content hash in the trailer); 2: milestone 9 (sender scope, join admission queue, creation queue overflow, destroy messages)

inline constexpr uint32_t kTrailerMagic = 0x47564643;  // 'C' 'F' 'V' 'G' in memory order
inline constexpr size_t kHashBytes = 8;                // the content hash, in front of the milestone 8 trailer
inline constexpr size_t kBaseTrailerBytes = 8;         // magic, version, max_players, flags: the milestone 8 trailer, at the very end
inline constexpr size_t kTrailerBytes = kHashBytes + kBaseTrailerBytes;
inline constexpr uint16_t kFirstHashedVersion = 3;     // a trailer of this version or later has the hash

}  // namespace version
