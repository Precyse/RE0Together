#include "character_owner.h"

#include <atomic>
#include <cstring>

#include "debug_stats.h"
#include "game.h"
#include "game_tick.h"
#include "log.h"
#include "net_pad.h"
#include "party_mode.h"

namespace {

using character_owner::Character;
using character_owner::Ownership;

constexpr int kNoSlot = control_rule::kNoOwner;
constexpr size_t kCharacterCount = 2;
// The host repeats OWNERSHIP so a guest whose game starts after joining (the usual order) still receives it.
constexpr uint32_t kResendFrames = 60;

NetClient* g_net = nullptr;

std::atomic<bool> g_linked{false};
std::atomic<uint8_t> g_localSlot{0};
std::atomic<uint8_t> g_hostSlot{0};
std::atomic<uint32_t> g_epoch{0};
std::atomic<bool> g_peerPresent{false};
std::atomic<bool> g_recompute{true};  // host: decide (and announce) the fixed owners again
std::atomic<int> g_owner[kCharacterCount] = {kNoSlot, kNoSlot};
uint32_t g_framesSinceSend = 0;

void setOwners(Ownership owners) {
    g_owner[static_cast<size_t>(Character::Billy)] = owners.billyOwnerSlot;
    g_owner[static_cast<size_t>(Character::Rebecca)] = owners.rebeccaOwnerSlot;
    debug_stats::set(debug_stats::Gauge::BillyOwner, owners.billyOwnerSlot);
    debug_stats::set(debug_stats::Gauge::RebeccaOwner, owners.rebeccaOwnerSlot);
}

void clearOwners() {
    g_owner[0] = g_owner[1] = kNoSlot;
    debug_stats::set(debug_stats::Gauge::BillyOwner, debug_stats::kUnknownOwner);
    debug_stats::set(debug_stats::Gauge::RebeccaOwner, debug_stats::kUnknownOwner);
}

void sendOwnership() {
    const Ownership owners{static_cast<uint8_t>(g_owner[static_cast<size_t>(Character::Billy)].load()),
                           static_cast<uint8_t>(g_owner[static_cast<size_t>(Character::Rebecca)].load())};
    g_net->send(character_owner::kMsgOwnership, true, proto::kSlotAll,
                {reinterpret_cast<const uint8_t*>(&owners), sizeof(owners)});
    g_framesSinceSend = 0;
}

// The host owns Rebecca; the first peer owns Billy.
void decide() {
    setOwners(Ownership{static_cast<uint8_t>(net_pad::peerSlot()), g_localSlot});
    g_recompute = false;
    logger::write("character_owner: billy=slot %d rebecca=slot %d", g_owner[0].load(), g_owner[1].load());
    sendOwnership();
}

void onTick() {
    if (!character_owner::isHost() || !g_peerPresent) return;
    if (g_recompute) {
        decide();
    } else if (++g_framesSinceSend >= kResendFrames) {
        sendOwnership();
    }
}

}  // namespace

namespace character_owner {

Character identify(uintptr_t player) {
    switch (game::readPointer(player)) {
        case game::kBillyVtable: return Character::Billy;
        case game::kRebeccaVtable: return Character::Rebecca;
        default: return Character::Unknown;
    }
}

uintptr_t find(Character character) {
    for (const uintptr_t player : {game::controlled(), game::partner()}) {
        if (player && identify(player) == character) return player;
    }
    return 0;
}

bool isHost() { return g_linked && g_localSlot == g_hostSlot; }

Character localCharacter() {
    for (const Character character : {Character::Billy, Character::Rebecca}) {
        if (isLocalOwned(character)) return character;
    }
    return Character::Unknown;
}

bool isHostSlot(uint8_t slot) { return g_linked && slot == g_hostSlot; }

const char* name(Character character) {
    switch (character) {
        case Character::Billy: return "Billy";
        case Character::Rebecca: return "Rebecca";
        default: return "unknown";
    }
}

namespace {

Control ownerControl(Character character) {
    if (character == Character::Unknown) return Control::Vanilla;
    return control_rule::byOwnership(net_pad::active(), isHost(), g_owner[static_cast<size_t>(character)], g_localSlot);
}

}  // namespace

Control controlOf(Character character) {
    const bool focused = character != Character::Unknown && identify(game::controlled()) == character;
    return control_rule::byPartyMode(ownerControl(character), party_mode::current(), focused);
}

bool isRemoteOwned(Character character) { return ownerControl(character) == Control::Remote; }

bool isLocalOwned(Character character) { return ownerControl(character) == Control::Local; }

void onSession(const SessionSnapshot& session) {
    const bool peerPresent = net_pad::active();
    const bool newEpoch = g_epoch.exchange(session.epoch) != session.epoch;
    const bool peerChanged = g_peerPresent.exchange(peerPresent) != peerPresent;
    g_linked = session.linked;
    g_localSlot = session.localSlot;
    g_hostSlot = session.hostSlot;
    if (!session.linked || !peerPresent) clearOwners();
    if (newEpoch || !peerPresent || peerChanged) g_recompute = true;
}

void onFrame(const GameFrame& frame) {
    if (frame.type != kMsgOwnership || frame.payload.size() != sizeof(Ownership) || isHost() ||
        frame.slot != g_hostSlot) {
        return;
    }
    Ownership owners;
    std::memcpy(&owners, frame.payload.data(), sizeof(owners));
    const bool changed = g_owner[0] != owners.billyOwnerSlot || g_owner[1] != owners.rebeccaOwnerSlot;
    setOwners(owners);
    if (changed) {
        logger::write("character_owner: received billy=slot %u rebecca=slot %u", owners.billyOwnerSlot,
                      owners.rebeccaOwnerSlot);
    }
}

void enable(NetClient& net) {
    g_net = &net;
    game_tick::addCallback("character_owner", onTick);
}

void focus(Character character) {
    const uintptr_t controlled = game::controlled();
    const uintptr_t partner = game::partner();
    if (controlled && partner && identify(partner) == character) game::swapControlled(partner, controlled);
}

}  // namespace character_owner
