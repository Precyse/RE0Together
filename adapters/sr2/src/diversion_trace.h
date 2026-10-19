#pragma once
#include <windows.h>

#include <cstdint>

#include "activity_state.h"
#include "rate_limit.h"

// The trace gate of the diversion thunks: a line is written with trace=1 only, and one that may be due every frame at most
// once a second or once per key.
namespace diversion_trace {

inline bool dueNow(rate_limit::RateLimit& limit) {
    uint32_t triggers = 0;
    return activity_state::tracing() && limit.allow(GetTickCount64(), triggers);
}

inline bool firstTime(rate_limit::OncePerKey& once, uint64_t key) { return activity_state::tracing() && once.firstTime(key); }

}  // namespace diversion_trace
