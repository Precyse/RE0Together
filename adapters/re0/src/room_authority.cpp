#include "room_authority.h"

#include <atomic>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "authority.h"
#include "character_owner.h"
#include "debug_stats.h"
#include "frame_router.h"
#include "game_state.h"
#include "game_tick.h"
#include "log.h"
#include "reject_counters.h"
#include "room_authority_rule.h"
#include "room_authority_wire.h"
#include "scene.h"
#include "state_net.h"

namespace {

using authority::AuthorityTable;
using authority::Outcome;
using authority::Record;
using authority_wire::kNoOwner;
using room_authority_rule::Outgoing;
using room_authority_wire::AuthMessage;

constexpr int kNoSlotShown = -1;  // the slot printed for an unowned room

std::mutex g_mutex;
AuthorityTable g_table;                  // guarded by g_mutex
bool g_assigning = false;                // guarded by g_mutex: the claim being applied names an owner other than its sender
bool g_dropping = false;                 // guarded by g_mutex: a peer left and its rooms are being freed
std::atomic<bool> g_linked{false};
std::atomic<uint8_t> g_local{kNoOwner};  // written under g_mutex
bool g_sessionKnown = false;             // guarded by g_mutex
uint32_t g_sessionEpoch = 0;             // guarded by g_mutex
std::map<uint8_t, uint32_t> g_peers;     // guarded by g_mutex: slot -> join serial of the peer last seen there
std::string g_shown;                     // game thread: the last overlay text

int shownSlot(uint8_t owner) { return owner == kNoOwner ? kNoSlotShown : owner; }

const char* causeOf(const Record& after) {
    if (after.owner == kNoOwner) return g_dropping ? "peer left" : "released";
    return g_assigning ? "assigned" : "claimed";
}

// The table's change handler (called under g_mutex). Barred-list changes alone are not an authority change.
void logChange(const Record& before, const Record& after) {
    if (before.owner == after.owner && before.epoch == after.epoch) return;
    logger::write("room_authority: scene 0x%02x owner slot %d epoch %u (%s)", static_cast<unsigned>(after.id),
                  shownSlot(after.owner), after.epoch, causeOf(after));
}

void sendAll(const std::vector<Outgoing>& messages) {
    for (const Outgoing& out : messages) state_net::send(out.type, true, proto::bytesOf(out.message));
}

void sendClaims(const std::vector<AuthMessage>& claims) {
    for (const AuthMessage& claim : claims) state_net::send(room_authority_wire::kMsgClaim, true, proto::bytesOf(claim));
}

// Net thread, through the frame router: any peer may send; the table's own sender rules judge the message.
void onMessage(const GameFrame& frame) {
    AuthMessage message;
    if (!room_authority_wire::decode(frame.payload, message)) {
        reject_counters::count(frame.type, reject_counters::Reason::Malformed);
        logger::writeUnlessRepeated("room_authority: type 0x%04x from slot %u refused malformed", frame.type, frame.slot);
        return;
    }
    std::lock_guard lock(g_mutex);
    g_assigning = frame.slot != message.owner;
    const Outcome outcome = g_table.onMessage(room_authority_wire::tableKind(frame.type), frame.slot, message);
    g_assigning = false;
    const auto reason = authority::rejectReason(outcome);
    if (!reason) return;
    reject_counters::count(frame.type, *reason);
    logger::write("room_authority: scene 0x%02x owner slot %d epoch %u (refused %s)", static_cast<unsigned>(message.id),
                  shownSlot(g_table.ownerOf(message.id)), g_table.epochOf(message.id), reject_counters::name(*reason));
}

// The own character is in the loaded room; a host without a peer yet already plays its chosen character.
bool ownInLoadedRoom() {
    return game_state::inCurrentRoom(character_owner::find(character_owner::plannedLocalCharacter()));
}

// "here e3" / "peer e3" / "none e3": the loaded room's authority for the overlay.
std::string overlayText(uint16_t loaded, uint8_t local) {
    std::lock_guard lock(g_mutex);
    const auto view = room_authority_rule::viewOf(g_table.ownerOf(loaded), local);
    return std::string(room_authority_rule::name(view)) + " e" + std::to_string(g_table.epochOf(loaded));
}

void onTick() {
    if (!g_linked) return;
    const uint8_t local = g_local;
    const uint16_t loaded = scene::current();
    std::vector<Outgoing> messages;
    {
        std::lock_guard lock(g_mutex);
        messages = room_authority_rule::reconcile(g_table, local, loaded, ownInLoadedRoom());
    }
    sendAll(messages);
    const std::string text = overlayText(loaded, local);
    if (text == g_shown) return;
    g_shown = text;
    debug_stats::setRoomAuthorityLine(g_shown);
}

// A new connection or a new session epoch starts the table over.
void startSession(const SessionSnapshot& session) {
    g_table.clear();
    g_table.setSession(session.localSlot, session.hostSlot);
    g_local = session.localSlot;
    g_sessionKnown = true;
    g_sessionEpoch = session.epoch;
    g_peers.clear();
}

}  // namespace

namespace room_authority {

void enable() {
    g_table.setChangeHandler(logChange);
    for (const uint16_t type : {room_authority_wire::kMsgClaim, room_authority_wire::kMsgDecline,
                                room_authority_wire::kMsgStop}) {
        frame_router::add(type, frame_router::SenderPolicy::Any, onMessage);
    }
    game_tick::addCallback("room_authority", onTick);
}

void onSession(const SessionSnapshot& session) {
    std::vector<AuthMessage> claims;
    {
        std::lock_guard lock(g_mutex);
        g_linked = session.linked;
        if (!session.linked) {
            g_sessionKnown = false;
            return;
        }
        if (!g_sessionKnown || session.epoch != g_sessionEpoch) startSession(session);
        std::map<uint8_t, uint32_t> now;
        for (const PeerInfo& peer : session.peers) now[peer.slot] = peer.joinSerial;
        g_dropping = true;
        for (const auto& [slot, serial] : g_peers) {
            const auto found = now.find(slot);
            if (found == now.end() || found->second != serial) g_table.dropSlot(slot);
        }
        g_dropping = false;
        bool newcomer = false;
        for (const auto& [slot, serial] : now) {
            const auto known = g_peers.find(slot);
            newcomer = newcomer || known == g_peers.end() || known->second != serial;
        }
        g_peers = std::move(now);
        if (newcomer) claims = g_table.claimsOf(session.localSlot);
    }
    sendClaims(claims);
}

void resendClaims() {
    std::vector<AuthMessage> claims;
    {
        std::lock_guard lock(g_mutex);
        claims = g_table.claimsOf(g_local);
    }
    sendClaims(claims);
}

bool runsHere(uint16_t sceneId) {
    if (!g_linked) return true;
    std::lock_guard lock(g_mutex);
    return g_table.isOwner(sceneId);
}

}  // namespace room_authority
