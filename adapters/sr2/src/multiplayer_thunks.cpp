#include "multiplayer_thunks.h"

#include "partner_thunks.h"
#include "population.h"
#include "sender_scope.h"

namespace multiplayer_thunks {

partner_thunks::Binding bindingOf(partner_sites::Thunk thunk) {
    for (const auto bind : {&sender_scope::bind, &population::bind}
) {
        const partner_thunks::Binding binding = bind(thunk);
        if (binding.entry) return binding;
    }
    return {nullptr, nullptr};
}

}  // namespace multiplayer_thunks
