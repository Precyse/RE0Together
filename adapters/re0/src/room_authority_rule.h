#pragma once
#include <cstdint>
#include <optional>
#include <vector>

#include "authority.h"
#include "room_authority_wire.h"
#include "scene.h"

// When this machine claims or gives up a room's enemy authority, and what goes on the wire for it. Pure: the table is
// DS2's `authority::AuthorityTable`, nothing here reads the game or sends.
namespace room_authority_rule {

using authority::AuthorityTable;
using authority_wire::kNoOwner;
using room_authority_wire::AuthMessage;

enum class Step : uint8_t { None, Claim, Release };

// What to do with one room's record. `loadedScene` is the room loaded here (scene::kNone while loading) and
// `ownInRoom` says this machine's own character is in it. A room is claimed when it is unowned and the own character
// is in it, and also when `takeOver` (this machine is the host and no boss fight runs) and the peer owns it: the host
// runs every room it is in, the enemies carrying on from the peer's stream (2026-10-25 live: a slow load gave the
// guest the lift room). It is released when this machine owns it and the own character is no longer in it (left it,
// or the room is not loaded).
constexpr Step stepFor(uint16_t scene, uint16_t loadedScene, bool ownInRoom, uint8_t owner, uint8_t local, bool takeOver = false) {
    const bool here = scene == loadedScene && loadedScene != scene::kNone && ownInRoom;
    if (owner == local && !here) return Step::Release;
    if (here && (owner == kNoOwner || (takeOver && owner != local))) return Step::Claim;
    return Step::None;
}

// A message the table produced, with this build's wire id.
struct Outgoing {
    uint16_t type;
    AuthMessage message;
};

// Every table change the local steps make, in order: the rooms this machine owns but is no longer in are released,
// then the loaded room is claimed when it is unowned. Empty when nothing changes.
inline std::vector<Outgoing> reconcile(AuthorityTable& table, uint8_t local, uint16_t loadedScene, bool ownInRoom,
                                       bool takeOver = false) {
    std::vector<Outgoing> out;
    for (const AuthMessage& owned : table.claimsOf(local)) {
        const uint16_t scene = static_cast<uint16_t>(owned.id);
        if (stepFor(scene, loadedScene, ownInRoom, table.ownerOf(owned.id), local) != Step::Release) continue;
        if (const auto stop = table.release(owned.id)) out.push_back({room_authority_wire::kMsgStop, *stop});
    }
    if (loadedScene != scene::kNone &&
        stepFor(loadedScene, loadedScene, ownInRoom, table.ownerOf(loadedScene), local, takeOver) == Step::Claim) {
        if (const auto claim = table.claim(loadedScene)) out.push_back({room_authority_wire::kMsgClaim, *claim});
    }
    return out;
}

// The room's authority as shown on the overlay: none (unowned), here (this machine runs its enemies), peer.
enum class View : uint8_t { None, Here, Peer };

constexpr View viewOf(uint8_t owner, uint8_t local) {
    if (owner == kNoOwner) return View::None;
    return owner == local ? View::Here : View::Peer;
}

constexpr const char* name(View view) {
    switch (view) {
        case View::None: return "none";
        case View::Here: return "here";
        case View::Peer: return "peer";
    }
    return "none";
}

}  // namespace room_authority_rule
