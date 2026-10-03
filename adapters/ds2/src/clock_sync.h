#pragma once
// Peer clocks: a ping to every peer (CLOCK_PING, unreliable) is answered at once with the peer's own time
// (CLOCK_PONG); each pong feeds that peer's ClockOffset estimate. Timestamps on the wire are the sender's `nowUs()`,
// and `toLocalUs` turns one into this machine's time, so a stream can be rendered a fixed delay behind real time
// whatever the two machines' clocks read.
#include <cstdint>
#include <optional>

#include "net_client.h"
#include "protocol.h"
#include "time_us.h"

namespace clock_sync {

constexpr uint16_t kMsgClockPing = proto::kFirstGameType + 0x14;  // 0x0114, to one slot, unreliable: ClockPing
constexpr uint16_t kMsgClockPong = proto::kFirstGameType + 0x15;  // 0x0115, to the pinger, unreliable: ClockPong

struct ClockPing {
    uint64_t sentUs;  // the pinger's nowUs() when it left
};
static_assert(sizeof(ClockPing) == 8);

struct ClockPong {
    uint64_t pingSentUs;  // the ping's stamp, echoed
    uint64_t peerUs;      // the answerer's nowUs() when it answered
};
static_assert(sizeof(ClockPong) == 16);

// Net thread.
void onFrame(NetClient& net, const GameFrame& frame);
void tick(NetClient& net, const SessionSnapshot& session);

// A time the peer in `slot` stamped, on this machine's clock; nothing until enough pongs came back. Any thread.
std::optional<TimeUs> toLocalUs(uint8_t slot, TimeUs peerUs);

// The median round trip to the peer, microseconds. Any thread.
std::optional<TimeUs> rttUs(uint8_t slot);

}  // namespace clock_sync
