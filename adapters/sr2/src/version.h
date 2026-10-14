#pragma once
#include <cstddef>
#include <cstdint>

// What identifies this mod build on the wire (docs/SR2_NOTES.md, "Version gate"). Every join_req and join_acc a modded
// peer sends ends with the trailer below; a peer without a matching one is refused.
namespace version {

// The one place that is bumped whenever the wire behaviour of the mod changes.
inline constexpr uint16_t kProtocolVersion = 1;

inline constexpr uint32_t kTrailerMagic = 0x47564643;  // 'C' 'F' 'V' 'G' in memory order
inline constexpr size_t kTrailerBytes = 8;

}  // namespace version
