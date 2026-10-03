#pragma once
// RESYNC: "send me your full state again". A machine that suspects it drifted (or just reached gameplay) asks a peer
// for whole snapshots of the parts named in `scopes`; each part's module answers from its own tick by asking
// `takeRequests(scope)` who is waiting. Net thread only, like every module that calls it.
#include <cstdint>
#include <vector>

#include "net_client.h"
#include "protocol.h"

namespace resync {

constexpr uint16_t kMsgResync = proto::kFirstGameType + 0x18;  // 0x0118, to one slot or all, reliable: ResyncRequest

enum Scope : uint32_t {
    kFacts = 1,      // the host's fact snapshot (fact_sync)
    kAuthority = 2,  // the claims the peer owns (authority_sync)
    kAnim = 4,       // a full ANIM_STATE snapshot (anim_sync)
    kAll = kFacts | kAuthority | kAnim
};

struct ResyncRequest {
    uint32_t scopes;
};
static_assert(sizeof(ResyncRequest) == 4);

// Asks `destSlot` (or everyone, proto::kSlotAll) to resend the parts in `scopes`. False while not linked.
inline bool request(NetClient& net, uint8_t destSlot, uint32_t scopes) {
    return net.send(kMsgResync, true, destSlot, proto::bytesOf(ResyncRequest{scopes}));
}

// The same as if every peer had asked: resend my own state to everyone (the manual trigger).
void requestLocal(uint32_t scopes);

void onFrame(const GameFrame& frame);

// The slots waiting for `scope` since the last call (proto::kSlotAll for a local request); the wait is cleared.
std::vector<uint8_t> takeRequests(uint32_t scope);

}  // namespace resync
