#include "enemy_sync.h"

#include <algorithm>
#include <span>
#include <vector>

#include "enemy_wire.h"
#include "game.h"
#include "log.h"
#include "resync.h"

namespace {

// Net thread only.
bool g_guest = false;
bool g_requestedAtGameplay = false;
uint8_t g_hostSlot = 0;
size_t g_knownPeers = 0;

void sendStates(NetClient& net, const std::vector<enemy_wire::EnemyState>& states) {
    for (size_t first = 0; first < states.size(); first += enemy_wire::kMaxStatesPerMessage) {
        const size_t count = std::min(enemy_wire::kMaxStatesPerMessage, states.size() - first);
        net.send(enemy_wire::kMsgEnemyState, false, proto::kSlotAll,
                 enemy_wire::encodeStates(std::span(states).subspan(first, count)));
    }
}

void sendHostReports(NetClient& net) {
    for (const enemy_wire::EnemySpawn& spawn : game::takeEnemySpawns()) {
        net.send(enemy_wire::kMsgEnemySpawn, true, proto::kSlotAll, proto::bytesOf(spawn));
    }
    for (const enemy_wire::EnemyGone& gone : game::takeEnemyGone()) {
        net.send(enemy_wire::kMsgEnemyGone, true, proto::kSlotAll, proto::bytesOf(gone));
    }
    sendStates(net, game::takeEnemyStates());
}

}  // namespace

namespace enemy_sync {

void onFrame(const GameFrame& frame) {
    if (!g_guest || frame.slot != g_hostSlot) return;
    if (frame.type == enemy_wire::kMsgEnemySpawn) {
        enemy_wire::EnemySpawn spawn;
        if (!enemy_wire::decodeOne(frame.payload, spawn)) {
            logger::write("enemy_sync: dropped a malformed ENEMY_SPAWN (%zu bytes)", frame.payload.size());
            return;
        }
        static bool first = true;
        if (first) logger::write("enemy_sync: the host's first enemy announcement arrived");
        first = false;
        game::puppetSpawn(spawn);
    } else if (frame.type == enemy_wire::kMsgEnemyGone) {
        enemy_wire::EnemyGone gone;
        if (enemy_wire::decodeOne(frame.payload, gone)) game::puppetGone(gone);
    } else if (frame.type == enemy_wire::kMsgEnemyState) {
        std::vector<enemy_wire::EnemyState> states;
        if (enemy_wire::decodeStates(frame.payload, states)) game::puppetStates(states);
    }
}

void tick(NetClient& net, const SessionSnapshot& session) {
    const bool host = session.linked && session.localSlot == session.hostSlot;
    g_guest = session.linked && !host;
    g_hostSlot = session.hostSlot;
    game::shareEnemies(host);
    if (host) {
        if (session.peers.size() > g_knownPeers || !resync::takeRequests(resync::kEnemies).empty()) {
            game::requestEnemySnapshot();
        }
        g_knownPeers = session.peers.size();
        sendHostReports(net);
        return;
    }
    g_knownPeers = 0;
    if (!g_guest || !game::gameplaySettled()) {
        g_requestedAtGameplay = false;
    } else if (!g_requestedAtGameplay && resync::request(net, g_hostSlot, resync::kEnemies)) {
        g_requestedAtGameplay = true;
    }
}

}  // namespace enemy_sync
