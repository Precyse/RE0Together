#include "lobby.h"

#include <array>
#include <atomic>
#include <chrono>
#include <mutex>
#include <string>

#include "appearance.h"
#include "appearance_rule.h"
#include "character_owner.h"
#include "coop_menu_rule.h"
#include "game.h"
#include "live_menus.h"
#include "log.h"
#include "option_values.h"
#include "save_blocks.h"
#include "save_load_rule.h"
#include "session_slot.h"
#include "state_mode.h"

namespace {

using Clock = std::chrono::steady_clock;
using lobby_wire::Place;
using lobby_wire::Presence;
using lobby_wire::SessionLoad;

constexpr auto kAnnounceInterval = std::chrono::seconds(2);
constexpr auto kPresenceStale = std::chrono::milliseconds(coop_menu_rule::kPresenceStaleMs);
static_assert(lobby_wire::kPlayerSlots == save_load_rule::kPlayerSlots);

struct Received {
    uint8_t slot;
    Presence presence;
    Clock::time_point at;
};

std::mutex g_mutex;  // guards the state below that the game thread reads
lobby_wire::LobbyState g_state;
bool g_haveState = false;
std::vector<Received> g_received;
Presence g_own{};
std::optional<SessionLoad> g_pendingStart;  // guest: the host's newest start not taken
uint32_t g_takenSerial = 0;
std::optional<SessionLoad> g_start;  // host: its last start, resent while it plays
uint32_t g_startSerial = 0;

// Net thread only.
NetClient* g_net = nullptr;
uint32_t g_seenRevision = 0;
Presence g_announced{};
Clock::time_point g_lastAnnounce;
Clock::time_point g_lastStartSend;
std::string g_appliedWorldPause;
std::atomic<Place> g_titlePlace{Place::Title};

bool inGame() { return game::controlled() != 0; }

// The launcher's values that act while the game runs: world pause at once; the host's character and this player's
// outfits only before the game starts (they are what the next game start uses).
void apply(const lobby_wire::LobbyState& state) {
    if (const lobby_wire::Value* pause = state.option(option_values::kWorldPauseKey); pause && pause->value != g_appliedWorldPause) {
        g_appliedWorldPause = pause->value;
        live_menus::configure(pause->value == option_values::kWorldPauseNever && state_mode::on());
    }
    if (inGame()) return;
    if (const lobby_wire::Value* character = state.option(option_values::kCharacterKey); character && character_owner::isHost()) {
        if (const auto chosen = option_values::characterOf(character->value)) character_owner::setHostCharacter(*chosen);
    }
    for (const character_owner::Character character : character_owner::kCharacters) {
        const lobby_wire::Value* outfit = state.ownValue(option_values::outfitKey(character));
        if (!outfit) continue;
        const auto model = outfit->value.empty() ? std::optional<uint8_t>(appearance_rule::kNoOutfit) : appearance_rule::parseModel(outfit->value);
        if (model && (*model == appearance_rule::kNoOutfit || appearance_rule::find(character, *model))) appearance::setChoice(character, *model);
    }
}

void takeLobbyState(const SessionSnapshot& session) {
    if (session.lobbyRevision == g_seenRevision || !session.lobbyState) return;
    g_seenRevision = session.lobbyRevision;
    lobby_wire::LobbyState state = lobby_wire::parseLobbyState(*session.lobbyState);
    logger::write("lobby: launcher state, %zu members", state.members.size());
    apply(state);
    std::lock_guard lock(g_mutex);
    g_state = std::move(state);
    g_haveState = true;
}

std::optional<Presence> hostPresence(uint8_t hostSlot) {
    std::lock_guard lock(g_mutex);
    for (const Received& received : g_received) {
        if (received.slot == hostSlot && Clock::now() - received.at <= kPresenceStale) return received.presence;
    }
    return std::nullopt;
}

// Where this game is and, for the host, the save it will start; for a guest, whether its copy of that save is the host's.
Presence ownPresence(const SessionSnapshot& session) {
    Presence presence{static_cast<uint8_t>(inGame() ? Place::InGame : g_titlePlace.load()), 0, lobby_wire::kNoSlot, 0, 0};
    if (character_owner::isHost()) {
        const save_load_rule::Order order = session_slot::order();
        // The block's own hash, as the guest computes it from its block: both sides compare the same kind of bytes.
        const std::optional<uint32_t> hash = order.active() ? save_blocks::hash(order.slot) : std::nullopt;
        if (hash) presence = {presence.place, lobby_wire::kHasPick, static_cast<uint8_t>(order.slot), 0, *hash};
        return presence;
    }
    const std::optional<Presence> host = hostPresence(session.hostSlot);
    if (!host || (host->flags & lobby_wire::kHasPick) == 0) return presence;
    const std::optional<uint32_t> mine = save_blocks::hash(host->pickSlot);
    presence.flags = lobby_wire::kSaveChecked | (mine && *mine == host->pickHash ? lobby_wire::kSaveMatches : 0);
    return presence;
}

bool samePresence(const Presence& a, const Presence& b) {
    return a.place == b.place && a.flags == b.flags && a.pickSlot == b.pickSlot && a.pickHash == b.pickHash;
}

void announcePresence(NetClient& net, const SessionSnapshot& session) {
    const auto now = Clock::now();
    const bool due = now - g_lastAnnounce >= kAnnounceInterval;
    // The hashes behind a presence read a whole slot block: worked out on a change of place or every 2 s, not every tick.
    const Place place = inGame() ? Place::InGame : g_titlePlace.load();
    if (!due && static_cast<uint8_t>(place) == g_announced.place) return;
    const Presence presence = ownPresence(session);
    {
        std::lock_guard lock(g_mutex);
        g_own = presence;
    }
    if (session.peers.empty() || (!due && samePresence(presence, g_announced))) return;
    if (!net.send(lobby_wire::kMsgLobbyPresence, true, proto::kSlotAll, proto::bytesOf(presence))) return;
    g_announced = presence;
    g_lastAnnounce = now;
}

// The host's start goes out again every 2 s while it plays, naming the slot the session plays now (a save since the
// start moved it to the co-op slot): a guest that reaches its title later follows it.
void resendStart(NetClient& net) {
    if (!character_owner::isHost() || !inGame() || Clock::now() - g_lastStartSend < kAnnounceInterval) return;
    SessionLoad start{};
    {
        std::lock_guard lock(g_mutex);
        if (!g_start) return;
        const int32_t slot = session_slot::current();
        if (save_load_rule::isPlayerSlot(slot)) {
            if (const auto hash = save_blocks::hash(slot)) g_start = SessionLoad{slot, *hash, g_start->serial};
        }
        start = *g_start;
    }
    if (net.send(lobby_wire::kMsgSessionLoad, true, proto::kSlotAll, proto::bytesOf(start))) g_lastStartSend = Clock::now();
}

}  // namespace

namespace lobby {

void onNetTick(NetClient& net, const SessionSnapshot& session) {
    g_net = &net;
    takeLobbyState(session);
    if (!session.linked) return;
    announcePresence(net, session);
    resendStart(net);
}

void onFrame(const GameFrame& frame) {
    if (frame.type == lobby_wire::kMsgLobbyPresence) {
        const std::optional<Presence> presence = lobby_wire::decodePresence(frame.payload);
        if (!presence) return;
        std::lock_guard lock(g_mutex);
        std::erase_if(g_received, [&](const Received& received) { return received.slot == frame.slot; });
        g_received.push_back({frame.slot, *presence, Clock::now()});
        return;
    }
    if (frame.type != lobby_wire::kMsgSessionLoad || character_owner::isHost() || !character_owner::isHostSlot(frame.slot)) return;
    const std::optional<SessionLoad> load = lobby_wire::decodeSessionLoad(frame.payload);
    if (!load) return;
    std::lock_guard lock(g_mutex);
    if (load->serial <= g_takenSerial) return;
    if (!g_pendingStart || g_pendingStart->serial != load->serial || g_pendingStart->slot != load->slot) {
        logger::write("lobby: the host started slot %d (start %u)", load->slot, load->serial);
    }
    g_pendingStart = load;
}

void setPlace(lobby_wire::Place place) { g_titlePlace = place; }

bool sendOption(std::string_view key, std::string_view value) {
    const std::optional<std::string> text = lobby_wire::encodeOptionSet(key, value);
    if (!text || !g_net) return false;
    const std::span<const uint8_t> bytes(reinterpret_cast<const uint8_t*>(text->data()), text->size());
    const bool sent = g_net->send(proto::kOptionSet, true, proto::kSlotAll, bytes);
    logger::write("lobby: option %s %s", text->c_str(), sent ? "sent to the launcher" : "not sent (no link)");
    return sent;
}

void announceStart(int32_t slot, uint32_t slotHash) {
    std::lock_guard lock(g_mutex);
    g_start = SessionLoad{slot, slotHash, ++g_startSerial};
    logger::write("lobby: start %u of slot %d (hash %08x) announced while in game", g_start->serial, slot, slotHash);
}

std::optional<lobby_wire::SessionLoad> takeStart() {
    std::lock_guard lock(g_mutex);
    if (!g_pendingStart) return std::nullopt;
    const SessionLoad load = *g_pendingStart;
    g_pendingStart.reset();
    g_takenSerial = load.serial;
    return load;
}

bool partnerLoading() {
    std::lock_guard lock(g_mutex);
    for (const Received& received : g_received) {
        if (Clock::now() - received.at <= kPresenceStale && received.presence.place == static_cast<uint8_t>(Place::Loading)) return true;
    }
    return false;
}

bool hostInGame() {
    std::lock_guard lock(g_mutex);
    for (const Received& received : g_received) {
        if (character_owner::isHostSlot(received.slot) && Clock::now() - received.at <= kPresenceStale) {
            return received.presence.place == static_cast<uint8_t>(Place::InGame);
        }
    }
    return false;
}

std::string nameOf(uint8_t slot) {
    std::lock_guard lock(g_mutex);
    for (const lobby_wire::Member& member : g_state.members) {
        if (member.slot == slot) return member.name;
    }
    return {};
}

View view() {
    View out;
    std::lock_guard lock(g_mutex);
    out.haveState = g_haveState;
    out.state = g_state;
    out.own = g_own;
    for (const Received& received : g_received) {
        if (Clock::now() - received.at <= kPresenceStale) out.peers.push_back({received.slot, received.presence});
    }
    return out;
}

}  // namespace lobby
