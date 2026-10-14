#pragma once
#include <cstddef>
#include <cstdint>

// The pure rules of the remote players' view frustums (milestone 9 W3, unit tested). The render frame builds the partner's
// frustum at 0x277CE00 (and tests spawns and despawns against it with 0xA020C0) only with two humans; here each remote has
// its own, and a test against the partner frustum asks all of them.
namespace frustum_rules {

constexpr uintptr_t kPartnerFrustum = 0x277CE00;  // count and planes, 0x210 bytes
constexpr size_t kFrustumBytes = 0x210;
constexpr size_t kBuildRemotesAtLeast = 2;  // with fewer remotes the game's own frustum is the only one

// A test with the frustum `pushed` asks every remote's frustum only when it is the partner frustum and there are several.
// With one remote it goes to the game's function untouched, which is the 2-player result.
inline bool asksEveryRemote(uintptr_t pushed, size_t remoteFrustums) {
    return pushed == kPartnerFrustum && remoteFrustums >= kBuildRemotesAtLeast;
}

}  // namespace frustum_rules
