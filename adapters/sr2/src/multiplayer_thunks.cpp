#include "multiplayer_thunks.h"

#include "creation_queue.h"
#include "cutscene_sync.h"
#include "join_admission.h"
#include "leave_path.h"
#include "partner_thunks.h"
#include "pause_mask.h"
#include "player_trace.h"
#include "population.h"
#include "sender_scope.h"

namespace multiplayer_thunks {

partner_thunks::Binding bindingOf(partner_sites::Thunk thunk) {
    for (const auto bind : {&sender_scope::bind, &population::bind, &creation_queue::bind, &join_admission::bind, &leave_path::bind, &pause_mask::bind, &cutscene_sync::bind, &player_trace::bind}




) {
        const partner_thunks::Binding binding = bind(thunk);
        if (binding.entry) return binding;
    }
    return {nullptr, nullptr};
}

}  // namespace multiplayer_thunks
