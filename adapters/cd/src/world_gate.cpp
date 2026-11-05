#include "world_gate.h"

#include <cmath>
#include <cstring>
#include <optional>
#include <string_view>

#include "actor_key.h"
#include "log.h"
#include "troc_catalogue.h"

namespace {

// MoveActorReq payload: actor key +0x00, world position f32 x3 +0x04 [N, STUDY.md 4].
constexpr size_t kMoveKey = 0x00;
constexpr size_t kMovePosition = 0x04;
constexpr size_t kMoveHeadNeeded = kMovePosition + 3 * sizeof(float);
static_assert(kMoveHeadNeeded <= funnels::kEventHeadBytes);

enum class Role : uint8_t { None, LoadStart, LoadComplete, Logout, OwnMove };

struct Watched {
    std::string_view name;
    packet::Direction direction;
    Role role;
};
constexpr Watched kWatched[] = {
    {"GameLoadingStartReq", packet::Direction::ClientToServer, Role::LoadStart},
    {"ReviveLoadingReq", packet::Direction::ClientToServer, Role::LoadStart},
    {"LoadingCompleteAck", packet::Direction::ServerToClient, Role::LoadComplete},
    {"LogoutCharacterToFieldServerReq", packet::Direction::ClientToServer, Role::Logout},
    {"LogoutUserToServerAck", packet::Direction::ServerToClient, Role::Logout},
    {"MoveActorReq", packet::Direction::ClientToServer, Role::OwnMove},
};

struct Resolved {
    uint16_t opcode = 0;
    packet::Direction direction = packet::Direction::ClientToServer;
    Role role = Role::None;
};
Resolved g_resolved[std::size(kWatched)];

world_gate_rule::Gate g_gate;
uint32_t g_ownPlayerKey = 0;
float g_position[3] = {};

Role roleOf(const funnels::Event& event) {
    for (const Resolved& r : g_resolved) {
        if (r.role != Role::None && r.opcode == event.header.opcode && r.direction == event.direction) return r.role;
    }
    return Role::None;
}

void logTransition(const world_gate_rule::Transition& t) {
    if (!t.changed()) return;
    logger::write("world_gate: %s -> %s (%s), load serial %u", world_gate_rule::name(t.from), world_gate_rule::name(t.to),
                  world_gate_rule::name(t.cause), g_gate.loadSerial());
}

void ownMove(const funnels::Event& event) {
    if (event.header.payloadSize < kMoveHeadNeeded) return;
    uint32_t key = 0;
    std::memcpy(&key, event.head.data() + kMoveKey, sizeof(key));
    if (actor_key::classOf(key) != actor_key::Class::Player) return;  // our horse moves through the same Req
    if (key != g_ownPlayerKey) logger::write("world_gate: own player key 0x%08X", key);
    g_ownPlayerKey = key;
    float position[3];
    std::memcpy(position, event.head.data() + kMovePosition, sizeof(position));
    if (std::isfinite(position[0]) && std::isfinite(position[1]) && std::isfinite(position[2])) {
        std::memcpy(g_position, position, sizeof(position));
    }
    g_gate.ownPlayerMoved();
}

}  // namespace

namespace world_gate {

bool watchOpcodes() {
    bool all = true;
    for (size_t i = 0; i < std::size(kWatched); ++i) {
        const std::optional<uint16_t> opcode = troc_catalogue::table().opcodeOf(kWatched[i].name);
        if (!opcode) {
            all = false;
            continue;
        }
        g_resolved[i] = {*opcode, kWatched[i].direction, kWatched[i].role};
        funnels::watch(*opcode);
    }
    if (!all) logger::write("world_gate: load bracket incomplete, the gate stays at Title");
    return all;
}

void onEvent(const funnels::Event& event, uint64_t nowMs) {
    switch (roleOf(event)) {
        case Role::LoadStart: logTransition(g_gate.loadStarted(nowMs)); break;
        case Role::LoadComplete: logTransition(g_gate.loadCompleted(nowMs)); break;
        case Role::Logout: logTransition(g_gate.loggedOut(nowMs)); break;
        case Role::OwnMove: ownMove(event); break;
        case Role::None: break;
    }
}

void tick(uint64_t nowMs) { logTransition(g_gate.tick(nowMs)); }

world_gate_rule::State state() { return g_gate.state(); }

wire_cd::World report() {
    wire_cd::World world{};
    world.loadSerial = g_gate.loadSerial();
    world.gateState = static_cast<uint8_t>(g_gate.state());
    std::memcpy(world.position, g_position, sizeof(g_position));
    world.ownPlayerKey = g_ownPlayerKey;
    return world;
}

}  // namespace world_gate
