#include "weapon_sync.h"

#include <chrono>
#include <map>
#include <optional>

#include "game.h"
#include "log.h"
#include "peer_joins.h"
#include "remote_body.h"
#include "weapon_wire.h"

namespace {

using Clock = std::chrono::steady_clock;

constexpr auto kPollInterval = std::chrono::milliseconds(100);

bool g_enabled = false;

// Net thread only.
Clock::time_point g_lastPoll;
std::optional<weapon_wire::WeaponState> g_reported;
PeerJoins g_joins;  // which peer joins have been sent the drawn weapon
std::map<uint8_t, weapon_wire::WeaponState> g_peerState;  // by source slot
std::optional<weapon_wire::WeaponState> g_applied;        // what the body was last told

// The local player's drawn weapon, sent when it changes and again to a peer that has just joined.
void reportState(NetClient& net, const SessionSnapshot& session) {
    const auto now = Clock::now();
    if (now - g_lastPoll < kPollInterval) return;
    g_lastPoll = now;
    const std::optional<weapon_wire::WeaponState> state = game::localWeaponState();
    if (!state) return;
    const bool joined = !g_joins.takeNew(session).empty();
    if (!joined && g_reported == state) return;
    g_reported = std::nullopt;  // reported again on the next poll when this send fails
    if (!net.send(weapon_wire::kMsgWeaponState, true, proto::kSlotAll, proto::bytesOf(*state))) return;
    g_reported = state;
    logger::write("weapon_sync: reported weapon %u (hand %u)", state->weaponId, state->hand);
}

void reportShots(NetClient& net, bool linked) {
    for (const weapon_wire::WeaponFire& fire : game::takeLocalFires()) {
        if (linked && !net.send(weapon_wire::kMsgWeaponFire, true, proto::kSlotAll, proto::bytesOf(fire))) {
            logger::write("weapon_sync: could not send a shot");
        }
    }
}

// The body shows the weapon of the peer it stands for, whichever order the state and the body appeared in.
void followPartner(const SessionSnapshot& session) {
    std::erase_if(g_peerState, [&session](const auto& entry) {
        for (const PeerInfo& peer : session.peers) {
            if (peer.slot == entry.first) return false;
        }
        return true;
    });
    const auto partner = g_peerState.find(remote_body::slot());
    const std::optional<weapon_wire::WeaponState> wanted =
        partner == g_peerState.end() ? std::nullopt : std::optional(partner->second);
    if (wanted == g_applied) return;
    g_applied = wanted;
    game::setPartnerWeapon(wanted.value_or(weapon_wire::WeaponState{weapon_wire::kHolstered, 0, 0}));
}

}  // namespace

namespace weapon_sync {

void setEnabled(bool enabled) { g_enabled = enabled; }

void onFrame(const GameFrame& frame) {
    if (!g_enabled) return;
    if (frame.type == weapon_wire::kMsgWeaponState) {
        weapon_wire::WeaponState state;
        if (weapon_wire::decode(frame.payload, state)) {
            g_peerState[frame.slot] = state;
        } else {
            logger::write("weapon_sync: dropped a malformed WEAPON_STATE (%zu bytes)", frame.payload.size());
        }
    } else if (frame.type == weapon_wire::kMsgWeaponFire && frame.slot == remote_body::slot()) {
        weapon_wire::WeaponFire fire;
        if (weapon_wire::decode(frame.payload, fire)) {
            game::partnerFire(fire);
        } else {
            logger::write("weapon_sync: dropped a malformed WEAPON_FIRE (%zu bytes)", frame.payload.size());
        }
    }
}

void tick(NetClient& net, const SessionSnapshot& session) {
    if (!g_enabled) return;
    reportShots(net, session.linked);
    if (!session.linked) return;
    reportState(net, session);
    followPartner(session);
}

}  // namespace weapon_sync
