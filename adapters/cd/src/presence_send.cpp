#include "presence_send.h"

#include <cmath>
#include <cstring>
#include <deque>
#include <string_view>
#include <vector>

#include "actor_key.h"
#include "link.h"
#include "log.h"
#include "presence_rule.h"
#include "template_rule.h"
#include "troc_catalogue.h"
#include "wire_cd.h"
#include "world_gate.h"

namespace {

constexpr uint64_t kTemplateSettleMs = 2000;  // after Ready, before the template goes out
constexpr size_t kUserCandidates = 4;
constexpr float kParkedY = 1000.0f;           // the default character of a fresh session is parked at (0, 1000, 0)
constexpr float kParkedTolerance = 1.0f;

struct Opcodes {
    uint16_t load = 0, move = 0, action = 0, rotate = 0;
} g_op;

struct UserCandidate {
    uint32_t playerKey = 0;
    std::vector<uint8_t> user;
    std::vector<uint8_t> body;  // empty until the body load that copies this user packet arrives
};

std::deque<UserCandidate> g_candidates;
std::vector<uint8_t> g_user, g_body;  // the current own template
uint32_t g_ownPlayer = 0, g_ownUser = 0;
uint16_t g_generation = 0, g_sentGeneration = 0;
uint32_t g_seq = 0;

std::optional<uint64_t> g_anchorStamp;
uint64_t g_anchorWallMs = 0;

bool g_moveSizeOk = false, g_actionSizeOk = false, g_sizeMismatchLogged = false;
std::optional<uint64_t> g_readySinceMs;
presence_send::Stats g_stats;

bool parkedDefault(const std::vector<uint8_t>& user) {
    float position[3];
    std::memcpy(position, user.data() + user.size() - template_rule::kPositionFromEnd, sizeof(position));
    return std::fabs(position[0]) < kParkedTolerance && std::fabs(position[1] - kParkedY) < kParkedTolerance &&
           std::fabs(position[2]) < kParkedTolerance;
}

void adoptIfOwn(const UserCandidate& candidate) {
    if (!g_ownPlayer || candidate.playerKey != g_ownPlayer || candidate.body.empty()) return;
    if (candidate.user == g_user && candidate.body == g_body) return;
    g_user = candidate.user;
    g_body = candidate.body;
    g_ownUser = template_rule::read<uint32_t>(g_user, template_rule::kUserKey);
    if (++g_generation == 0) g_generation = 1;
    logger::write("presence_send: own template generation %u (user %zu B, body %zu B, keys 0x%08X 0x%08X)", g_generation,
                  g_user.size(), g_body.size(), g_ownUser, g_ownPlayer);
}

void onLoad(const std::vector<uint8_t>& payload) {
    if (payload.size() < template_rule::kMinUser) return;
    const uint32_t first = template_rule::read<uint32_t>(payload, template_rule::kUserKey);
    if (actor_key::classOf(first) == actor_key::Class::User) {
        const uint32_t owned = template_rule::read<uint32_t>(payload, template_rule::kOwnedKey);
        if (actor_key::classOf(owned) != actor_key::Class::Player || parkedDefault(payload)) return;
        if (g_candidates.size() == kUserCandidates) g_candidates.pop_front();
        g_candidates.push_back({owned, payload, {}});
        return;
    }
    if (actor_key::classOf(first) != actor_key::Class::World) return;
    for (UserCandidate& candidate : g_candidates) {
        if (candidate.body.empty() && template_rule::truncateBody(payload, candidate.user)) {
            candidate.body = payload;
            adoptIfOwn(candidate);
            return;
        }
    }
}

void anchor(uint64_t stamp, uint64_t nowMs) {
    g_anchorStamp = stamp;
    g_anchorWallMs = nowMs;
}

void checkSize(bool& ok, size_t got, size_t want, const char* name) {
    if (got == want) {
        ok = true;
        return;
    }
    if (!g_sizeMismatchLogged) {
        g_sizeMismatchLogged = true;
        logger::write("presence_send: %s is %zu bytes on this build, the recipe expects %zu: presence off", name, got, want);
    }
    ok = false;
}

bool ready() { return world_gate::state() == world_gate_rule::State::Ready; }

void send(uint16_t type, bool reliable, std::span<const uint8_t> body) {
    const wire_cd::StreamHeader header{world_gate::report().loadSerial, g_generation, ++g_seq};
    link::send(type, reliable, wire_cd::encodeStream(header, body));
}

void onOwnReq(const funnels::Event& event, uint64_t nowMs) {
    const std::vector<uint8_t>& req = event.payload;
    if (req.size() < sizeof(uint32_t)) return;
    const uint32_t key = template_rule::read<uint32_t>(req, presence_rule::kKey);
    if (actor_key::classOf(key) != actor_key::Class::Player) return;  // our horse moves through the same Reqs
    const uint16_t op = event.header.opcode;
    if (op == g_op.move) {
        checkSize(g_moveSizeOk, req.size(), presence_rule::kMoveReqSize, "MoveActorReq");
        if (!g_moveSizeOk) return;
        if (key != g_ownPlayer) {
            g_ownPlayer = key;
            for (const UserCandidate& candidate : g_candidates) adoptIfOwn(candidate);
        }
        anchor(template_rule::read<uint64_t>(req, presence_rule::kMoveTimestamp), nowMs);
        if (!ready() || !g_sentGeneration) return;
        if (const auto compact = presence_rule::compactMove(req)) {
            send(wire_cd::kCharMove, false, *compact);
            ++g_stats.movesSent;
        }
    } else if (op == g_op.action) {
        checkSize(g_actionSizeOk, req.size(), presence_rule::kActionSize, "StartActionReq");
        if (!g_actionSizeOk || key != g_ownPlayer) return;
        anchor(template_rule::read<uint64_t>(req, presence_rule::kActionReqTimestamp), nowMs);
        if (!ready() || !g_sentGeneration) return;
        send(wire_cd::kCharAction, true, req);
        ++g_stats.actionsSent;
    } else if (op == g_op.rotate) {
        if (req.size() != presence_rule::kRotateReqSize || key != g_ownPlayer) return;
        anchor(template_rule::read<uint64_t>(req, presence_rule::kRotateTimestamp), nowMs);
        if (ready() && g_sentGeneration) send(wire_cd::kCharRotate, false, req);
    }
}

}  // namespace

namespace presence_send {

bool watchOpcodes() {
    const troc_table::Table& table = troc_catalogue::table();
    const auto load = table.opcodeOf("ActorLoadDataAck"), move = table.opcodeOf("MoveActorReq"),
               action = table.opcodeOf("StartActionReq"), rotate = table.opcodeOf("RotateActorReq");
    if (!load || !move || !action || !rotate) return false;
    g_op = {*load, *move, *action, *rotate};
    for (uint16_t op : {g_op.load, g_op.move, g_op.action, g_op.rotate}) funnels::watch(op, true);
    return true;
}

void onEvent(const funnels::Event& event, uint64_t nowMs) {
    if (event.payload.empty()) return;
    if (event.direction == packet::Direction::ServerToClient) {
        if (event.header.opcode == g_op.load) onLoad(event.payload);
        return;
    }
    onOwnReq(event, nowMs);
}

void tick(uint64_t nowMs, bool peerJoined) {
    if (!ready()) {
        g_readySinceMs.reset();
        return;
    }
    if (!g_readySinceMs) g_readySinceMs = nowMs;
    if (!g_generation || !recipesMatch() || nowMs - *g_readySinceMs < kTemplateSettleMs) return;
    if (g_sentGeneration == g_generation && !peerJoined) return;
    const wire_cd::TemplateHeader header{g_generation, world_gate::report().loadSerial, static_cast<uint32_t>(g_user.size()),
                                         static_cast<uint32_t>(g_body.size())};
    if (link::send(wire_cd::kCharTemplate, true, wire_cd::encodeTemplate(header, g_user, g_body))) {
        g_sentGeneration = g_generation;
        ++g_stats.templatesSent;
    }
}

std::optional<uint64_t> ownStampNow(uint64_t nowMs) {
    if (!g_anchorStamp) return std::nullopt;
    return *g_anchorStamp + (nowMs - g_anchorWallMs);
}

bool recipesMatch() { return g_moveSizeOk && g_actionSizeOk; }

uint32_t ownUserKey() { return g_ownUser; }
uint32_t ownPlayerKey() { return g_ownPlayer; }

uint16_t generation() { return g_generation; }

Stats stats() { return g_stats; }

}  // namespace presence_send
