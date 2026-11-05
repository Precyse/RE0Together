#include "presence_recv.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "containment.h"
#include "funnels.h"
#include "link.h"
#include "log.h"
#include "presence_rule.h"
#include "presence_send.h"
#include "stream_clock_rule.h"
#include "template_rule.h"
#include "troc_catalogue.h"
#include "wire_cd.h"
#include "world_gate.h"

namespace {

constexpr uint64_t kStillMoveAfterMs = 200;   // the still Move that follows the first action
constexpr uint64_t kLoginGraceMs = 2000;      // before streaming Acks to a new copy (its login must finish first)
constexpr uint64_t kSilenceMs = 8000;         // no frame from the peer this long: the copy goes
constexpr uint64_t kSpawnStormWindowMs = 10000;
constexpr uint32_t kSpawnStormMax = 4;

struct Opcodes {
    uint16_t load = 0, remove = 0, move = 0, action = 0, rotate = 0;
} g_op;

struct Copy {
    uint8_t slot = 0;
    std::vector<uint8_t> user, body;  // the peer's template as received
    uint16_t generation = 0;
    std::optional<std::vector<uint8_t>> lastMove;    // compact
    std::optional<std::vector<uint8_t>> lastAction;  // a self-contained StartAction Req
    bool spawned = false;
    uint64_t spawnedAtMs = 0;
    uint32_t spawnCount = 0;
    bool stillMoveSent = false;
    uint64_t lastFrameMs = 0;
    std::string refusal;  // the last template refusal, logged once per generation
    std::optional<int64_t> lastStamp;
    stream_clock_rule::MoveThinner thinner;
    std::vector<uint64_t> spawnTimes;
};

bool g_enabled = false;
std::map<uint8_t, Copy> g_copies;
presence_recv::Stats g_stats;

using presence_recv::canInject;

void injectAck(uint16_t opcode, std::span<const uint8_t> payload) { funnels::inject(presence_rule::packetOf(opcode, payload)); }

bool peerReady(uint8_t slot) {
    const auto world = link::peerWorld(slot);
    return world && world->gateState == static_cast<uint8_t>(world_gate_rule::State::Ready);
}


// A stamp on our own clock, strictly increasing for this copy.
std::optional<uint64_t> stampFor(Copy& copy, uint64_t nowMs) {
    const auto now = presence_send::ownStampNow(nowMs);
    if (!now) return std::nullopt;
    const int64_t stamp = stream_clock_rule::restamp(static_cast<int64_t>(*now), static_cast<int64_t>(*now), copy.lastStamp);
    copy.lastStamp = stamp;
    return static_cast<uint64_t>(stamp);
}

void remove(Copy& copy, const char* why) {
    if (!copy.spawned) return;
    copy.spawned = false;
    if (!canInject()) return;  // inside a load window nothing is injected (our own load drops the copies anyway)
    const template_rule::Keys keys = template_rule::copyKeys(copy.slot);
    for (uint32_t key : {keys.player, keys.body}) injectAck(g_op.remove, presence_rule::removeAck(key));
    logger::write("presence_recv: slot %u copy removed (%s)", copy.slot, why);
}

void spawn(Copy& copy, uint64_t nowMs) {
    std::erase_if(copy.spawnTimes, [&](uint64_t at) { return nowMs - at > kSpawnStormWindowMs; });
    if (copy.spawnTimes.size() >= kSpawnStormMax) return;
    template_rule::Placement at;
    std::memcpy(at.position, copy.lastMove->data() + presence_rule::kMovePosition, sizeof(at.position));
    std::memcpy(at.rotation, copy.lastMove->data() + presence_rule::kMoveQuaternion, sizeof(at.rotation));
    std::string why;
    const auto made = template_rule::makeCopy(copy.user, copy.body, copy.slot, at,
                                              {presence_send::ownUserKey(), presence_send::ownPlayerKey()}, why);
    if (!made) {
        if (why != copy.refusal) logger::write("presence_recv: slot %u template generation %u refused (%s)", copy.slot, copy.generation, why.c_str());
        copy.refusal = why;
        return;
    }
    const auto stamp = stampFor(copy, nowMs);
    if (!stamp) return;
    const uint32_t key = template_rule::copyKeys(copy.slot).player;
    injectAck(g_op.load, made->user);
    injectAck(g_op.load, made->body);
    // A copy is its load plus a first action: an action layer with a chart but no playback crashes a worker thread.
    const auto still = presence_rule::moveAck(*copy.lastMove, key, *stamp);
    if (copy.lastAction) {
        if (const auto action = presence_rule::actionAck(*copy.lastAction, key, *stamp)) injectAck(g_op.action, *action);
    } else if (still) {
        injectAck(g_op.move, *still);
    }
    copy.spawned = true;
    copy.spawnedAtMs = nowMs;
    ++copy.spawnCount;
    copy.stillMoveSent = false;
    copy.spawnTimes.push_back(nowMs);
    ++g_stats.templatesApplied;
    logger::write("presence_recv: slot %u copy spawned from generation %u at (%.1f, %.1f, %.1f)", copy.slot, copy.generation,
                  at.position[0], at.position[1], at.position[2]);
}

void onTemplate(Copy& copy, const wire_cd::Template& t) {
    if (t.header.generation == copy.generation && !copy.user.empty()) return;
    remove(copy, "new template generation");
    copy.user.assign(t.user.begin(), t.user.end());
    copy.body.assign(t.body.begin(), t.body.end());
    copy.generation = t.header.generation;
    copy.refusal.clear();
}

void onMove(Copy& copy, std::span<const uint8_t> compact, uint64_t nowMs) {
    copy.lastMove.emplace(compact.begin(), compact.end());
    if (!copy.spawned || nowMs - copy.spawnedAtMs < kLoginGraceMs || !canInject()) return;
    const uint64_t peerStamp = presence_rule::read<uint64_t>(compact, presence_rule::kMoveTimestamp);
    const uint64_t parent = presence_rule::read<uint64_t>(compact, presence_rule::kMoveParent);
    if (!copy.thinner.keep(nowMs, parent, static_cast<int64_t>(peerStamp))) return;
    const auto stamp = stampFor(copy, nowMs);
    if (!stamp) return;
    if (const auto ack = presence_rule::moveAck(compact, template_rule::copyKeys(copy.slot).player, *stamp)) {
        injectAck(g_op.move, *ack);
        ++g_stats.movesApplied;
    }
}

void onAction(Copy& copy, std::span<const uint8_t> req, uint64_t nowMs) {
    if (!presence_rule::actionIsSelfContained(req)) {
        ++g_stats.actionsHeld;  // names a mount or a target: needs the key map (phase 5)
        return;
    }
    copy.lastAction.emplace(req.begin(), req.end());
    if (!copy.spawned || nowMs - copy.spawnedAtMs < kLoginGraceMs || !canInject()) return;
    const auto stamp = stampFor(copy, nowMs);
    if (!stamp) return;
    if (const auto ack = presence_rule::actionAck(req, template_rule::copyKeys(copy.slot).player, *stamp)) {
        injectAck(g_op.action, *ack);
        ++g_stats.actionsApplied;
    }
}

void onRotate(Copy& copy, std::span<const uint8_t> req, uint64_t nowMs) {
    if (!copy.spawned || nowMs - copy.spawnedAtMs < kLoginGraceMs || !canInject()) return;
    const auto stamp = stampFor(copy, nowMs);
    if (!stamp) return;
    if (const auto ack = presence_rule::rotateAck(req, template_rule::copyKeys(copy.slot).player, *stamp)) injectAck(g_op.rotate, *ack);
}

void tickCopy(Copy& copy, uint64_t nowMs) {
    if (copy.spawned && nowMs - copy.lastFrameMs > kSilenceMs) remove(copy, "8 s of silence");
    if (!copy.spawned) {
        if (canInject() && containment::active() && peerReady(copy.slot) && !copy.user.empty() && copy.lastMove) spawn(copy, nowMs);
        return;
    }
    if (!copy.stillMoveSent && nowMs - copy.spawnedAtMs >= kStillMoveAfterMs && copy.lastMove && canInject()) {
        copy.stillMoveSent = true;
        const auto stamp = stampFor(copy, nowMs);
        const auto ack = stamp ? presence_rule::moveAck(*copy.lastMove, template_rule::copyKeys(copy.slot).player, *stamp) : std::nullopt;
        if (ack) injectAck(g_op.move, *ack);
    }
}

}  // namespace

namespace presence_recv {

bool resolveOpcodes() {
    const troc_table::Table& table = troc_catalogue::table();
    const auto load = table.opcodeOf("ActorLoadDataAck"), remove = table.opcodeOf("RemoveActorAck"),
               move = table.opcodeOf("MoveActorAck"), action = table.opcodeOf("StartActionAck"),
               rotate = table.opcodeOf("RotateActorAck");
    if (!load || !remove || !move || !action || !rotate) return false;
    g_op = {*load, *remove, *move, *action, *rotate};
    return true;
}

void setEnabled(bool on) { g_enabled = on; }

bool canInject() {
    return g_enabled && funnels::injectionOn() && presence_send::recipesMatch() &&
           world_gate::state() == world_gate_rule::State::Ready;
}

std::optional<CopyView> copyView(uint8_t slot, uint64_t nowMs) {
    const auto it = g_copies.find(slot);
    if (it == g_copies.end() || !it->second.spawned) return std::nullopt;
    const Copy& copy = it->second;
    return CopyView{copy.generation, copy.spawnCount, nowMs - copy.spawnedAtMs >= kLoginGraceMs};
}

bool onFrame(uint8_t slot, uint16_t type, std::span<const uint8_t> payload, uint64_t nowMs) {
    Copy& copy = g_copies[slot];
    copy.slot = slot;
    copy.lastFrameMs = nowMs;
    switch (type) {
        case wire_cd::kCharTemplate:
            if (const auto t = wire_cd::decodeTemplate(payload)) {
                onTemplate(copy, *t);
                return true;
            }
            return false;
        case wire_cd::kCharMove:
            if (const auto s = wire_cd::decodeStream(payload, presence_rule::kMoveCompactSize)) {
                onMove(copy, s->body, nowMs);
                return true;
            }
            return false;
        case wire_cd::kCharAction:
            if (const auto s = wire_cd::decodeStream(payload, presence_rule::kActionSize)) {
                onAction(copy, s->body, nowMs);
                return true;
            }
            return false;
        case wire_cd::kCharRotate:
            if (const auto s = wire_cd::decodeStream(payload, presence_rule::kRotateReqSize)) {
                onRotate(copy, s->body, nowMs);
                return true;
            }
            return false;
        case wire_cd::kCharGone:
            remove(copy, "the peer left");
            return payload.size() == sizeof(wire_cd::Gone);
        default:
            return false;
    }
}

void tick(uint64_t nowMs, std::span<const uint8_t> slotsPresent) {
    if (world_gate::state() != world_gate_rule::State::Ready) {
        for (auto& [slot, copy] : g_copies) copy.spawned = false;  // our own load: the client dropped every copy
    }
    for (auto it = g_copies.begin(); it != g_copies.end();) {
        if (std::find(slotsPresent.begin(), slotsPresent.end(), it->first) == slotsPresent.end()) {
            remove(it->second, "the peer is gone");
            it = g_copies.erase(it);
            continue;
        }
        tickCopy(it->second, nowMs);
        ++it;
    }
}

Stats stats() { return g_stats; }

}  // namespace presence_recv
