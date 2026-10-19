#include "multiplayer_thunks.h"

#include "creation_queue.h"
#include "cutscene_sync.h"
#include "join_admission.h"
#include "leave_path.h"
#include "member_left.h"
#include "mission_players.h"
#include "net_clock.h"
#include "partner_thunks.h"
#include "pause_mask.h"
#include "player_trace.h"
#include "remote_frustum.h"
#include "population.h"
#include "script_trace.h"
#include "sender_scope.h"
#include "spawn_budget.h"
#include "traffic_trace.h"
#include "vehicle_trace.h"

namespace multiplayer_thunks {

partner_thunks::Binding bindingOf(partner_sites::Thunk thunk) {
    for (const auto bind : {&sender_scope::bind, &population::bind, &creation_queue::bind, &join_admission::bind, &leave_path::bind, &pause_mask::bind, &member_left::bind, &cutscene_sync::bind, &player_trace::bind, &net_clock::bind, &spawn_budget::bind, &vehicle_trace::bind, &traffic_trace::bind, &partner_thunks::bindCalls, &remote_frustum::bind, &mission_players::bind, &script_trace::bind}




) {
        const partner_thunks::Binding binding = bind(thunk);
        if (binding.entry) return binding;
    }
    return {nullptr, nullptr};
}

}  // namespace multiplayer_thunks
