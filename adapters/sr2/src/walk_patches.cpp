#include "walk_patches.h"

#include <array>
#include <iterator>

#include "activity_state.h"
#include "activity_thunks.h"
#include "diversion_thunks.h"
#include "log.h"
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

bool sitesMatch() {
    bool any = false;
    for (size_t i = 0; i < std::size(walk_sites::kSets); ++i) {
        const walk_sites::Set& set = walk_sites::kSets[i];
        const uintptr_t differs = site_installer::firstDifference(set.sites);
        g_matched[i] = differs == 0;
        any = any || g_matched[i];
        if (differs) {
            logger::write("walks: set %s differs at 0x%X, not installed", set.name, static_cast<unsigned>(differs));
        } else {
            logger::write("walks: set %s matches (%zu rows)", set.name, set.sites.size());
        }
    }
    return any;
}

void install(bool trace) {
    activity_state::setTracing(trace);
    for (size_t i = 0; i < std::size(walk_sites::kSets); ++i) {
        if (!g_matched[i]) continue;
        const walk_sites::Set& set = walk_sites::kSets[i];
        const site_installer::Result result = site_installer::install(set.sites, false, &bindingOf);
        if (result.installed) {
            logger::write("walks: set %s patched (%zu rows)", set.name, result.patched);
        } else {
            logger::write("walks: set %s not patched", set.name);
        }
    }
}

}  // namespace walk_patches
