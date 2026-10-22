#include "walk_patches.h"

#include <array>
#include <iterator>

#include "activity_state.h"
#include "activity_thunks.h"
#include "diversion_thunks.h"
#include "mayhem_blocks.h"
#include "partner_thunks.h"
#include "site_installer.h"
#include "walk_sites.h"
#include "walk_thunks.h"

namespace {

// Which sets matched the exe at start-up.
std::array<bool, std::size(walk_sites::kSets)> g_matched{};

// The rows share thunks with the earlier milestones: the each-remote calls of the diversions (DWK1, DWK2), the freeze of the activities
// (DWF5) and the identity match of the partner logic (DWP2).
partner_thunks::Binding bindingOf(partner_sites::Thunk thunk) {
    for (const auto bind : {&walk_thunks::bind, &mayhem_blocks::bind, &diversion_thunks::bind, &activity_thunks::bindGames,
                            &partner_thunks::bindCalls}) {
        const partner_thunks::Binding binding = bind(thunk);
        if (binding.entry) return binding;
    }
    return {nullptr, nullptr};
}

}  // namespace

namespace walk_patches {

bool sitesMatch() { return site_installer::matchSets("walks", walk_sites::kSets, g_matched); }

void install(bool trace) {
    activity_state::setTracing(trace);
    site_installer::installMatchedSets("walks", walk_sites::kSets, g_matched, &bindingOf);
}

}  // namespace walk_patches
