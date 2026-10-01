#include "net_trace.h"

#include <chrono>
#include <cstdio>
#include <string>

#include "log.h"
#include "net_client.h"
#include "pad_frame.h"
#include "paths.h"
#include "state_sync.h"

namespace {

using Clock = std::chrono::steady_clock;

constexpr auto kFlushInterval = std::chrono::seconds(1);

FILE* g_file = nullptr;  // net thread only once enabled
Clock::time_point g_start;
Clock::time_point g_lastFlush;

bool traced(uint16_t type) { return type == pad::kMsgPadFrame || type == state_sync::kMsgPlayerState; }

}  // namespace

namespace net_trace {

void enable() {
    const std::wstring path = coopDirectory() + L"\\net_trace.bin";
    g_file = _wfopen(path.c_str(), L"wb");
    g_start = g_lastFlush = Clock::now();
    logger::write(g_file ? "net_trace: recording to %ls" : "net_trace: cannot open %ls", path.c_str());
}

void record(const GameFrame& frame) {
    if (!g_file || !traced(frame.type)) return;
    const auto now = Clock::now();
    const TraceRecord header{static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now - g_start).count()),
                             frame.type, static_cast<uint16_t>(frame.payload.size())};
    std::fwrite(&header, sizeof(header), 1, g_file);
    std::fwrite(frame.payload.data(), 1, frame.payload.size(), g_file);
    if (now - g_lastFlush < kFlushInterval) return;
    std::fflush(g_file);
    g_lastFlush = now;
}

}  // namespace net_trace
