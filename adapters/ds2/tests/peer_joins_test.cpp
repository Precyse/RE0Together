// PeerJoins: a join is reported once per PEER_UP, also when a peer replaces another on the same slot without the number of
// peers changing; a slot that left is new again when it returns; clear() makes every peer new (no game).
#include <cstdio>

#include "../src/peer_joins.h"

namespace {

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_failures;
}

SessionSnapshot session(std::initializer_list<std::pair<int, int>> peers) {
    SessionSnapshot snapshot;
    for (const auto& [slot, serial] : peers) {
        PeerInfo peer;
        peer.slot = static_cast<uint8_t>(slot);
        peer.joinSerial = static_cast<uint32_t>(serial);
        snapshot.peers.push_back(peer);
    }
    return snapshot;
}

}  // namespace

int main() {
    PeerJoins joins;
    check(joins.takeNew(session({})).empty(), "no peers: nothing new");
    check(joins.takeNew(session({{1, 1}})) == std::vector<uint8_t>{1}, "a peer joins: its slot is new");
    check(joins.takeNew(session({{1, 1}})).empty(), "the same peer next tick: nothing new");
    check(joins.takeNew(session({{1, 2}})) == std::vector<uint8_t>{1}, "a peer takes the slot of one not yet dropped: new");
    check(joins.takeNew(session({})).empty(), "the peer leaves: nothing new");
    check(joins.takeNew(session({{1, 3}})) == std::vector<uint8_t>{1}, "it rejoins: new again");
    check(joins.takeNew(session({{1, 3}, {2, 4}})) == std::vector<uint8_t>{2}, "a second peer: only its slot is new");
    joins.clear();
    check(joins.takeNew(session({{1, 3}, {2, 4}})).size() == 2, "after clear every peer is new");
    std::printf(g_failures ? "%d FAILED\n" : "all passed\n", g_failures);
    return g_failures ? 1 : 0;
}
