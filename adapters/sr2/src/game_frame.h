#pragma once
#include <atomic>
#include <cstdint>

// The game thread's last frame (0x697780), stamped by the watchdog's frame hook on the game thread and read by the watch thread
// and the tunnel's readers (docs/SR2_NOTES.md, "Watchdog and exits", "Stalled peers and rejoins"). 0 before the first frame.
namespace game_frame {

inline std::atomic<uint64_t> g_lastFrameMs{0};
inline std::atomic<uint32_t> g_threadId{0};

// On the game thread, at each frame.
inline void stamp(uint64_t nowMs, uint32_t threadId) {
    if (g_threadId.load(std::memory_order_relaxed) == 0) g_threadId.store(threadId, std::memory_order_relaxed);
    g_lastFrameMs.store(nowMs, std::memory_order_relaxed);
}

inline uint64_t lastFrameMs() { return g_lastFrameMs.load(std::memory_order_relaxed); }
inline uint32_t threadId() { return g_threadId.load(std::memory_order_relaxed); }

}  // namespace game_frame
