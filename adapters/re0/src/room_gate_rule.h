#pragma once
#include <cstdint>

// Pure rule of the door barrier (no game access, unit tested). When one player takes a door in TEAM, both machines play
// the same door transition and load the room inside it; the room starts (room phase DoorLoad -> Main) when the door
// finishes. Load times differ by seconds between two PCs, so the faster machine's door waits at its finish until the
// peer's door is ready to finish too, and both rooms start their enemies together. Only that shared door ever waits:
// never a door one player takes alone, never a partner merely heading somewhere.
namespace room_gate_rule {

constexpr int64_t kMaxHoldMs = 5000;      // a peer that is not ready by then has stalled; play on
constexpr int64_t kWaitingToastMs = 2000;  // a hold this long is shown ("Waiting for partner")
static_assert(kWaitingToastMs < kMaxHoldMs);

// The peer's last ROOM_STATE: its loaded scene, and its running door (target, shared by both machines, ready to finish).
struct Peer {
    uint16_t scene;
    uint16_t doorTarget;
    bool doorShared;
    bool doorReady;
};

enum class Verdict { Hold, PeerReady, PeerNotComing, Timeout, NoPeer };

// This machine's door into `here` is ready to finish: wait only when both machines play this door and the peer's is
// not ready yet.
constexpr bool holdsAtFinish(bool ownDoorShared, uint16_t here, const Peer& peer) {
    return ownDoorShared && peer.doorShared && peer.doorTarget == here && !peer.doorReady && peer.scene != here;
}

// While holding the door into `here`, `heldMs` after the hold started.
constexpr Verdict check(uint16_t here, bool peerKnown, const Peer& peer, int64_t heldMs) {
    if (!peerKnown) return Verdict::NoPeer;
    if (peer.scene == here || (peer.doorTarget == here && peer.doorReady)) return Verdict::PeerReady;
    if (peer.doorTarget != here) return Verdict::PeerNotComing;
    return heldMs >= kMaxHoldMs ? Verdict::Timeout : Verdict::Hold;
}

constexpr bool showsWaiting(int64_t heldMs) { return heldMs >= kWaitingToastMs; }

constexpr const char* name(Verdict verdict) {
    switch (verdict) {
        case Verdict::Hold: return "holding";
        case Verdict::PeerReady: return "peer ready";
        case Verdict::PeerNotComing: return "peer not coming";
        case Verdict::Timeout: return "timeout";
        case Verdict::NoPeer: return "no peer";
    }
    return "?";
}

}  // namespace room_gate_rule
