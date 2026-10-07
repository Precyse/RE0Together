#pragma once
#include <cstdint>

// Pure rule of the room-entry barrier (no game access, unit tested). The engine loads a room after its door animation,
// and load times differ by seconds between two PCs, so the machine that arrives first would run the room's enemies
// alone until the other arrives. A machine that arrives in a room the peer is still travelling to holds its world until
// the peer is there too; both rooms then start from their spawn records together.
namespace room_gate_rule {

constexpr int64_t kMaxHoldMs = 15000;  // a peer that does not arrive by then has stalled; play on

// What the peer last reported (door_travel::RoomState): its loaded scene and the scene its running door leads to.
struct Peer {
    uint16_t scene;
    uint16_t doorTarget;
};

enum class Verdict { Hold, PeerArrived, PeerNotComing, Timeout, NoPeer };

// On arriving in `here`: wait only for a peer whose door is taking it here.
constexpr bool holdsAtArrival(uint16_t here, const Peer& peer) { return peer.doorTarget == here && peer.scene != here; }

// While holding for `here`, `heldMs` after the arrival.
constexpr Verdict check(uint16_t here, bool peerKnown, const Peer& peer, int64_t heldMs) {
    if (!peerKnown) return Verdict::NoPeer;
    if (peer.scene == here) return Verdict::PeerArrived;
    if (peer.doorTarget != here) return Verdict::PeerNotComing;
    return heldMs >= kMaxHoldMs ? Verdict::Timeout : Verdict::Hold;
}

constexpr const char* name(Verdict verdict) {
    switch (verdict) {
        case Verdict::Hold: return "holding";
        case Verdict::PeerArrived: return "peer arrived";
        case Verdict::PeerNotComing: return "peer not coming";
        case Verdict::Timeout: return "timeout";
        case Verdict::NoPeer: return "no peer";
    }
    return "?";
}

}  // namespace room_gate_rule
