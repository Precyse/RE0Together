#pragma once
// When a peer counts as gone (pure, unit tested). Every plugin resends PLACE every 2 s, so a peer from which nothing
// arrived for three of those periods is not in the session any more as far as the modules are concerned.
#include <cstdint>

namespace session_rule {

constexpr uint64_t kPeerStaleMs = 6000;

inline bool stale(uint64_t nowMs, uint64_t lastFrameMs) { return nowMs > lastFrameMs && nowMs - lastFrameMs > kPeerStaleMs; }

}  // namespace session_rule
