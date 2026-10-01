#pragma once
#include <cstdint>

struct GameFrame;

// Opt-in recording (adapter.ini `net_trace=1`) of the partner's PAD_FRAME and PLAYER_STATE packets with their
// arrival time, to <game dir>\coop\net_trace.bin. The trace_replay tool runs a recording through the real
// PadBuffer to tune input delay without a second player online.
namespace net_trace {

// File layout: a sequence of records, each a TraceRecord followed by `size` payload bytes.
struct TraceRecord {
    uint32_t ms;  // arrival, milliseconds since recording started
    uint16_t type;
    uint16_t size;
};
static_assert(sizeof(TraceRecord) == 8);

// Opens the trace file. Call once at startup when the setting is on.
void enable();

// Net thread: appends the frame when recording and the type is traced.
void record(const GameFrame& frame);

}  // namespace net_trace
