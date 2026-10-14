#pragma once
#include "partner_sites.h"
#include "partner_thunks.h"

// The code behind the sites of activity_sites.h (milestone 7), split by area: activity_framework.cpp (who a message
// came from, starting an activity, shared start and cleanup), activity_credit.cpp (who gets the credit, the nearest
// player, who a message goes to) and activity_games.cpp (the activities and diversions themselves).
namespace activity_thunks {

// Each returns {nullptr, nullptr} for a thunk that belongs to another file.
partner_thunks::Binding bindFramework(partner_sites::Thunk thunk);
partner_thunks::Binding bindCredit(partner_sites::Thunk thunk);
partner_thunks::Binding bindGames(partner_sites::Thunk thunk);

}  // namespace activity_thunks
