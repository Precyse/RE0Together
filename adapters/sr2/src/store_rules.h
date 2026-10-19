#pragma once
#include <cstdint>

#include "sr2_addresses.h"

// The pure rules of milestone 16 wave 2 (no game access, unit tested; docs/SR2_NOTES.md, "Store, clothing and activity level menus
// (milestone 16 wave 2)"): where a message goes, and what the store hide does to a human's visibility flags.
namespace store_rules {

// One member only, or the game's own broadcast.
enum class Route { Broadcast, Member };

// The activity level menu request (act 0x0B): to the member that owns the activity, the broadcast when it is not a known remote.
constexpr Route levelMenuRoute(uintptr_t ownerMember) { return ownerMember != 0 ? Route::Member : Route::Broadcast; }

// A guest's choice or cancel of that menu: to the host alone, so that another guest never reads it as a request. The host and a
// session without a known host member keep the game's broadcast.
constexpr Route answerRoute(bool isHost, uintptr_t hostMember) { return !isHost && hostMember != 0 ? Route::Member : Route::Broadcast; }

// The visibility flags of a human after the store hides it (both hidden bits set) or shows it again (both cleared); other bits stay.
constexpr uint32_t withHidden(uint32_t flags, bool hide) { return hide ? flags | sr2::kHumanHiddenFlags : flags & ~sr2::kHumanHiddenFlags; }

}  // namespace store_rules
