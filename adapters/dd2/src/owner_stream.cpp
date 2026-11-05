#include "owner_stream.h"

#include "session.h"
#include "wire_dd2.h"
#include "world_gate.h"

namespace {

constexpr uint64_t kHeldPeriodMs = 250;  // 4 Hz while held
constexpr uint64_t kUsPerMs = 1000;

// One stream per local body (the Arisen and the main pawn); sequence numbers are per sender, shared by both.
struct Stream {
    std::optional<local_body::Sample> last;
    uint32_t lastHash[local_body::kSampledLayers] = {};
};

Stream g_streams[wire_dd2::kBodyKinds];
uint32_t g_stateSeq = 0, g_actionSeq = 0;
uint64_t g_lastHeldSendMs = 0;

void sendActionEdges(uint8_t kind, Stream& stream, uint64_t nowMs) {
    for (uint32_t layer = 0; layer < local_body::kSampledLayers; ++layer) {
        if (stream.last->actionHash[layer] == stream.lastHash[layer]) continue;
        stream.lastHash[layer] = stream.last->actionHash[layer];
        wire_dd2::ActionEvent event{};
        event.seq = ++g_actionSeq;
        event.senderUs = nowMs * kUsPerMs;
        event.kind = kind;
        event.layer = static_cast<uint8_t>(layer);
        event.actionHash = stream.last->actionHash[layer];
        session::sendMessage(wire_dd2::kActionEvent, true, event);
    }
}

void sendState(uint8_t kind, const local_body::Sample& sample, bool plain, uint64_t nowMs) {
    wire_dd2::BodyState state{};
    state.seq = ++g_stateSeq;
    state.senderUs = nowMs * kUsPerMs;
    state.kind = kind;
    state.flags = static_cast<uint8_t>((plain ? 0 : wire_dd2::kHeld) | (sample.dead ? wire_dd2::kDead : 0));
    state.loadSerial = world_gate::loadSerial();
    for (int axis = 0; axis < 3; ++axis) state.position[axis] = sample.position[axis];
    state.yaw = sample.yaw;
    state.speedType = static_cast<float>(sample.speedType);
    for (uint32_t layer = 0; layer < local_body::kSampledLayers; ++layer) state.actionHash[layer] = sample.actionHash[layer];
    session::sendMessage(wire_dd2::kBodyState, false, state);
}

}  // namespace

namespace owner_stream {

void tick(uint64_t nowMs) {
    const bool plain = world_gate::place() == world_gate_rule::Place::InGame;
    // The Arisen's last sample stays through a load (sent held); a main pawn that is gone (lost, at the inn) stops.
    for (uint8_t kind = 0; kind < wire_dd2::kBodyKinds; ++kind) {
        const auto sample = local_body::sample(kind);
        if (sample || kind != wire_dd2::kKindArisen) g_streams[kind].last = sample;
    }
    if (!plain && nowMs - g_lastHeldSendMs < kHeldPeriodMs) return;
    if (!plain) g_lastHeldSendMs = nowMs;
    for (uint8_t kind = 0; kind < wire_dd2::kBodyKinds; ++kind) {
        Stream& stream = g_streams[kind];
        if (!stream.last) continue;
        sendState(kind, *stream.last, plain, nowMs);
        if (plain) sendActionEdges(kind, stream, nowMs);
    }
}

std::optional<local_body::Sample> lastSample() { return g_streams[wire_dd2::kKindArisen].last; }

}  // namespace owner_stream
