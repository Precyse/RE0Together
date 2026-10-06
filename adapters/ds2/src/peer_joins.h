#pragma once
// Which peer joins a host has already answered (a snapshot sent, a state reported). A join is a PEER_UP, numbered by the net
// client (`PeerInfo::joinSerial`): a peer that reconnects fast and takes the slot of one the host has not yet dropped is a new
// join even though the number of peers did not change.
#include <cstdint>
#include <map>
#include <vector>

#include "net_client.h"

class PeerJoins {
public:
    // The slots whose peer joined since the last call; slots that left are forgotten, so a rejoin is new again.
    std::vector<uint8_t> takeNew(const SessionSnapshot& session) {
        std::vector<uint8_t> joined;
        std::map<uint8_t, uint32_t> now;
        for (const PeerInfo& peer : session.peers) {
            now[peer.slot] = peer.joinSerial;
            const auto known = seen_.find(peer.slot);
            if (known == seen_.end() || known->second != peer.joinSerial) joined.push_back(peer.slot);
        }
        seen_ = std::move(now);
        return joined;
    }

    // Forgets every join (this machine stopped being the host): the next peers are all new.
    void clear() { seen_.clear(); }

private:
    std::map<uint8_t, uint32_t> seen_;
};
