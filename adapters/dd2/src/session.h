#pragma once
// The launcher link and the session messages (GAME_HELLO, PLACE): this machine's identity and place to every peer,
// what each peer reported, and the routing of every other game frame to its module. Driven by the game-thread tick.
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "wire_dd2.h"

namespace session {

// Starts the launcher client (its own thread).
void start(uint16_t port, const wire_dd2::GameHello& hello);

// Game thread, from the 30 Hz tick: polls the link, hands each frame from a matching peer to its module, sends
// GAME_HELLO to joiners and PLACE on change and every 2 s.
void tick(uint64_t nowMs);

// To every peer. False while not linked.
bool send(uint16_t type, bool reliable, std::span<const uint8_t> payload);

template <typename T>
bool sendMessage(uint16_t type, bool reliable, const T& message) {
    const auto payload = wire_dd2::encode(message);
    return send(type, reliable, payload);
}

// Linked to the launcher with at least one peer whose plugin matches ours.
bool active();
bool isHost();

// The host's last PLACE, when this machine is a guest and has one.
std::optional<wire_dd2::Place> hostPlace();

// A peer's last PLACE, if it reported one and is not stale.
std::optional<wire_dd2::Place> peerPlace(uint8_t slot);

// The slots of the peers whose plugin matches ours and that are not stale.
std::vector<uint8_t> matchingPeers();

// Nothing arrived from the peer for session_rule::kPeerStaleMs. Its puppets and mirrors are released through the
// peerPlace / hostPlace / matchingPeers paths; true also for a slot that is not a peer.
bool stale(uint8_t slot);

// A peer as the launcher and the overlay report it.
struct PeerView {
    uint8_t slot;
    uint64_t steamId;
    std::string name;
    bool helloOk;  // its plugin matches ours
    bool stale;
    std::optional<wire_dd2::Place> place;  // its last PLACE, even a stale one
};

// Every other player of the session, whatever their plugin and staleness.
std::vector<PeerView> peers();

// The host's slot while linked to the launcher.
std::optional<uint8_t> hostSlot();

}  // namespace session
