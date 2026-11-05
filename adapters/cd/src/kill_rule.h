#pragma once
// Kills with attribution (pure, unit tested; cd design world.md 4.4): the host commits every death once (first lethal
// wins), and the credit policy says what each world does so that each player's own server credits its own player.
#include <cstdint>
#include <optional>
#include <set>
#include <utility>

namespace kill_rule {

enum class Killer : uint8_t { Host, Guest, Other };  // Other: an NPC or the environment

enum CreditBit : uint8_t { kCreditHost = 1, kCreditGuest = 2 };

// What the host's server does with the enemy's death.
enum class HostAction : uint8_t {
    Native,             // the host's own blow: nothing to do
    FinishHostCredit,   // down to 1 HP, then the finisher with the host's player as owner (reason 8)
    FinishNoCredit,     // down to 1 HP, then KillCheatReq option 0 (reason 16, no XP)
};

// What the guest's own server does with the shadow (or the hidden counterpart) of that enemy.
enum class GuestAction : uint8_t {
    Native,              // the guest's blow already killed the shadow natively (guest XP and loot)
    FinishGuestCredit,   // set to 1 HP, finished with the guest's player as owner (guest XP)
    EndNoCredit,         // ended without credit
};

struct Credit {
    HostAction host;
    GuestAction guest;
    uint8_t mask;  // CreditBit set of the players who get XP
};

constexpr Credit credit(Killer killer, bool xpShare) {
    switch (killer) {
        case Killer::Guest:
            return xpShare ? Credit{HostAction::FinishHostCredit, GuestAction::Native, kCreditHost | kCreditGuest}
                           : Credit{HostAction::FinishNoCredit, GuestAction::Native, kCreditGuest};
        case Killer::Host:
            return xpShare ? Credit{HostAction::Native, GuestAction::FinishGuestCredit, kCreditHost | kCreditGuest}
                           : Credit{HostAction::Native, GuestAction::EndNoCredit, kCreditHost};
        case Killer::Other:
            break;
    }
    return {HostAction::Native, GuestAction::EndNoCredit, 0};
}

struct Commit {
    uint32_t killSerial;
    uint8_t killerSlot;
    uint8_t mask;
};

// The host's kill ledger: one commit per (host key, host load serial).
class Ledger {
public:
    std::optional<Commit> commit(uint32_t hostKey, uint16_t hostSerial, uint8_t killerSlot, Killer killer, bool xpShare) {
        if (!committed_.emplace(hostKey, hostSerial).second) return std::nullopt;
        return Commit{++serial_, killerSlot, credit(killer, xpShare).mask};
    }

    // A new host load: keys are reused, so the old commits no longer apply.
    void reset() { committed_.clear(); }

private:
    std::set<std::pair<uint32_t, uint16_t>> committed_;
    uint32_t serial_ = 0;
};

}  // namespace kill_rule
