#pragma once
#include <cstdint>

#include "partner_sites.h"
#include "partner_thunks.h"
#include "party_ai_rules.h"

// The code behind the sites of party_ai_sites.h (milestone 16 wave 2), split by area: party_thunks.cpp (PA1 to PA3, PT1; this
// header's bind and trace flag), npc_ai_thunks.cpp (AI1 to AI11) and npc_team_odds.cpp (AI12).
namespace party_ai_thunks {

// {nullptr, nullptr} for a thunk none of the three files knows.
partner_thunks::Binding bind(partner_sites::Thunk thunk);

// Each returns {nullptr, nullptr} for a thunk that belongs to another file.
partner_thunks::Binding bindParty(partner_sites::Thunk thunk);
partner_thunks::Binding bindNpcAi(partner_sites::Thunk thunk);
partner_thunks::Binding bindTeamOdds(partner_sites::Thunk thunk);

// With trace on the thunks log the lines of the trace (party: ..., npc ai: ...).
void setTrace(bool trace);
bool traceOn();

// True when the game's busy test (0x985630) holds for `human`.
bool humanIsBusy(uintptr_t human);

// Counts one expanded evaluation of `which` for the per-minute line of the trace (nothing without trace); `differs` when the answer
// is not what the default partner alone would give.
void count(party_ai_rules::Pair party_ai_rules::AiCounters::*which, bool differs);

}  // namespace party_ai_thunks
