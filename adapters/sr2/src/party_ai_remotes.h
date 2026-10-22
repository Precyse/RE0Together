#pragma once
#include <cstdint>

#include "party_ai_rules.h"
#include "remote_players.h"

// The remote player humans as the thunks that ask every remote instead of the one default partner read them (NPC AI, FUZZ, Crowd
// Control): the humans now in ascending slot order and whether a thunk takes the shipped path.
namespace party_ai_remotes {

class Remotes {
public:
    Remotes() {
        remote_players::take(snapshot_);
        humans_ = party_ai_rules::remoteHumansOf(snapshot_.pairing);
        shipped_ = party_ai_rules::shippedPath(snapshot_.pairing.trusted, humans_.count);
    }

    const party_ai_rules::HumanList& humans() const { return humans_; }
    bool shipped() const { return shipped_; }
    uint8_t slotOf(uintptr_t human) const { return party_ai_rules::slotOfHuman(snapshot_.pairing, human); }

    // The remote the game's getters answer when no override is set, 0 for none or an untrusted pairing.
    uintptr_t defaultHuman() const { return remote_players::defaultPartner(snapshot_).human; }

private:
    remote_players::Snapshot snapshot_;
    party_ai_rules::HumanList humans_;
    bool shipped_;
};

}  // namespace party_ai_remotes
