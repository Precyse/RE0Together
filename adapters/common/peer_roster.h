#pragma once
// The other players of the session as the launcher reports them (pure, unit tested by the CD adapter's rules_test),
// with a per-peer state the adapter keeps. A peer that rejoins (a new join serial on its slot) starts with a fresh
// state. Shared by the CD and DD2 adapters.
#include <cstdint>
#include <map>

#include "net_client.h"

template <typename State>
class PeerRoster {
public:
    // Call once per tick with the client's snapshot. True when the link just came up or a peer joined or rejoined:
    // the caller then announces itself again.
    bool update(const SessionSnapshot& session) {
        if (!session.linked) {
            wasLinked_ = false;
            return false;
        }
        bool fresh = !wasLinked_;
        wasLinked_ = true;
        std::map<uint8_t, Entry> next;
        for (const PeerInfo& info : session.peers) {
            if (info.slot == session.localSlot) continue;
            Entry entry;
            const auto known = peers_.find(info.slot);
            if (known != peers_.end() && known->second.joinSerial == info.joinSerial) {
                entry = known->second;
            } else {
                entry.joinSerial = info.joinSerial;
                fresh = true;
            }
            next.emplace(info.slot, entry);
        }
        peers_ = std::move(next);
        return fresh && !peers_.empty();
    }

    // The state of a current peer, or null for a slot that is not one (a frame from a peer that already left).
    State* find(uint8_t slot) {
        const auto it = peers_.find(slot);
        return it == peers_.end() ? nullptr : &it->second.state;
    }

    bool empty() const { return peers_.empty(); }

private:
    struct Entry {
        uint32_t joinSerial = 0;
        State state{};
    };
    std::map<uint8_t, Entry> peers_;
    bool wasLinked_ = false;
};
