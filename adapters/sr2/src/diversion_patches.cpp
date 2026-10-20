#include "diversion_patches.h"

#include <array>
#include <iterator>

#include "activity_state.h"
#include "activity_thunks.h"
#include "diversion_sites.h"
#include "diversion_thunks.h"
#include "partner_thunks.h"
#include "site_installer.h"

namespace {

// Which sets matched the exe at start-up.
std::array<bool, std::size(diversion_sites::kSets)> g_matched{};

// The identity and nearest thunks of milestones 4 and 7 serve the same call patterns as the new ones, and the start warp
// of an all-players activity serves the Trafficking end (DVK5).
partner_thunks::Binding bindingOf(partner_sites::Thunk thunk) {
    const partner_thunks::Binding own = diversion_thunks::bind(thunk);
    if (own.entry) return own;
    for (const auto bind : {&activity_thunks::bindFramework, &activity_thunks::bindCredit, &partner_thunks::bindCalls}) {
        const partner_thunks::Binding binding = bind(thunk);
        if (binding.entry) return binding;
    }
    return {nullptr, nullptr};
}

}  // namespace

namespace diversion_patches {

bool sitesMatch() { return site_installer::matchSets("diversions", diversion_sites::kSets, g_matched); }

void install(bool trace) {
    activity_state::setTracing(trace);
    site_installer::installMatchedSets("diversions", diversion_sites::kSets, g_matched, &bindingOf);
}

}  // namespace diversion_patches
