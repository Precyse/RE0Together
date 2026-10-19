#pragma once
#include <cstddef>
#include <cstdint>

#include "guest_call.h"
#include "memory.h"
#include "partner_rules.h"
#include "remote_players.h"
#include "sr2_addresses.h"

// The remote players as the diversion thunks see them: the session now, and which of its remotes the game's getters answer
// by default.
namespace diversion_remotes {

struct View {
    remote_players::Snapshot snapshot;
    size_t defaultIndex = partner_rules::kNone;  // into pairing.items, kNone without a default partner

    // False outside co-op and when a remote human matches no member: the game's own answers stand.
    bool usable() const { return snapshot.active && snapshot.pairing.trusted; }
    const partner_rules::Remote* items() const { return snapshot.pairing.items; }
    size_t count() const { return snapshot.pairing.count; }
    size_t humanCount() const { return snapshot.humanCount; }  // remote player humans, paired or not
    uintptr_t defaultHuman() const { return defaultIndex < count() ? items()[defaultIndex].human : 0; }

    // The index of the remote that owns `human`, kNone for any other human.
    size_t indexOf(uintptr_t human) const {
        for (size_t i = 0; human && i < count(); ++i) {
            if (items()[i].human == human) return i;
        }
        return partner_rules::kNone;
    }
};

// The three floats at `address` (the game's vec3 inside an object).
inline partner_rules::Vec3 vec3At(uintptr_t address) { return *reinterpret_cast<const partner_rules::Vec3*>(address); }

// The vehicle object the human is in, 0 on foot (the game's own lookup, which accepts a vehicle only).
inline uintptr_t vehicleOf(uintptr_t human) {
    const uint32_t handle = readDword(human + sr2::kHumanVehicleOffset);
    return handle ? guest_call::call(sr2::kVehicleObjectOf, {.ecx = handle}) : 0;
}

inline void take(View& view) {
    remote_players::take(view.snapshot);
    const remote_players::Partner partner = remote_players::defaultPartner(view.snapshot);
    view.defaultIndex = partner.trusted ? view.indexOf(partner.human) : partner_rules::kNone;
}

}  // namespace diversion_remotes
