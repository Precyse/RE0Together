#include "world_time.h"

#include <optional>

#include "cd_sites.h"
#include "field_time_rule.h"
#include "link.h"
#include "log.h"
#include "presence_rule.h"
#include "safe_read.h"
#include "troc_catalogue.h"
#include "wire_cd.h"
#include "world_gate.h"

namespace {

constexpr uint64_t kSendPeriodMs = 5000;
constexpr uint64_t kUsPerMs = 1000;

struct Opcodes {
    uint16_t setReq = 0, setAck = 0;
} g_op;

uint64_t g_lastSentMs = 0;
std::optional<wire_cd::FieldTimeMessage> g_host;
uint64_t g_hostAtMs = 0;
field_time_rule::CatchUp g_catchUp;
world_time::Stats g_stats;

// The server clock as the client sees it (ms): the frame clock plus the server bias; null before the game set them.
std::optional<uint64_t> serverNow() {
    uint64_t ms = 0, us = 0;
    int64_t bias = 0;
    if (!safe_read::value(cd_sites::kClientClockMs, ms) || !safe_read::value(cd_sites::kClientClockUs, us) ||
        !safe_read::value(cd_sites::kServerClockBias, bias) || ms == 0 || bias == 0) {
        return std::nullopt;
    }
    return static_cast<uint64_t>(static_cast<int64_t>(ms + us / kUsPerMs) + bias);
}

std::optional<field_time_rule::FieldTime> ownTime() {
    uint8_t bytes[field_time_rule::kMemorySize];
    if (!safe_read::bytes(cd_sites::kFieldTimeClient, bytes, sizeof(bytes))) return std::nullopt;
    return field_time_rule::fromMemory(bytes);
}

bool readyAndRunning() {
    return world_gate::state() == world_gate_rule::State::Ready && !(world_gate::report().flags & wire_cd::kPaused);
}

void sendOwn(uint64_t nowMs) {
    if (nowMs - g_lastSentMs < kSendPeriodMs) return;
    const auto own = ownTime();
    const auto now = serverNow();
    if (!own || !now) return;
    const wire_cd::FieldTimeMessage message{own->rawAt(*now), own->scale,
                                            static_cast<uint8_t>(own->fixed() ? wire_cd::kFixedTime : 0)};
    if (link::send(wire_cd::kFieldTime, true, wire_cd::encode(message))) g_lastSentMs = nowMs;
}

void catchUp(uint64_t nowMs) {
    if (!g_host || (g_host->flags & wire_cd::kFixedTime)) return;
    const auto own = ownTime();
    const auto now = serverNow();
    if (!own || !now || own->fixed() || *now == UINT64_MAX) return;
    const uint64_t hosts = g_host->raw + (nowMs - g_hostAtMs) * g_host->scale;  // carried forward since it arrived
    const auto target = g_catchUp.decide(own->rawAt(*now), hosts, nowMs);
    if (!target) return;
    field_time_rule::FieldTime set = *own;
    set.setRaw(*target);
    set.anchor = *now;  // the server carries it forward from here, on its own clock
    funnels::post(presence_rule::packetOf(g_op.setReq, field_time_rule::setReq(set)));
    ++g_stats.posted;
    logger::write("world_time: own time set to day %u %02u:%02u (the host's hour)", set.day, set.hour, set.minute);
}

}  // namespace

namespace world_time {

bool resolveOpcodes() {
    const troc_table::Table& table = troc_catalogue::table();
    const auto setReq = table.opcodeOf("SetFieldTimeReq"), setAck = table.opcodeOf("SetFieldTimeAck");
    if (!setReq || !setAck) return false;
    g_op = {*setReq, *setAck};
    funnels::watch(g_op.setAck);
    return true;
}

void onEvent(const funnels::Event& event) {
    if (g_op.setAck && event.direction == packet::Direction::ServerToClient && event.header.opcode == g_op.setAck) {
        ++g_stats.confirmed;
    }
}

void tick(uint64_t nowMs) {
    if (!g_op.setReq || !readyAndRunning()) return;
    if (link::isHost()) {
        sendOwn(nowMs);
    } else {
        catchUp(nowMs);
    }
}

bool onFrame(uint8_t slot, uint16_t type, std::span<const uint8_t> payload, uint64_t nowMs) {
    if (type != wire_cd::kFieldTime) return false;
    const auto message = wire_cd::decodeFieldTime(payload);
    if (!message || link::hostSlot() != slot) return false;
    g_host = *message;
    g_hostAtMs = nowMs;
    return true;
}

Stats stats() { return g_stats; }

}  // namespace world_time
