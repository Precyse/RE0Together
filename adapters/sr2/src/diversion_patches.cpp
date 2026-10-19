#include "diversion_patches.h"

#include <array>
#include <iterator>

#include "activity_state.h"
#include "activity_thunks.h"
#include "diversion_sites.h"
#include "diversion_thunks.h"
#include "log.h"
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

bool sitesMatch() {
    bool any = false;
    for (size_t i = 0; i < std::size(diversion_sites::kSets); ++i) {
        const diversion_sites::Set& set = diversion_sites::kSets[i];
        const uintptr_t differs = site_installer::firstDifference(set.sites);
        g_matched[i] = differs == 0;
        any = any || g_matched[i];
        if (differs) {
            logger::write("diversions: set %s differs at 0x%X, not installed", set.name, static_cast<unsigned>(differs));
        } else {
            logger::write("diversions: set %s matches (%zu rows)", set.name, set.sites.size());
        }
    }
    return any;
}

void install(bool trace) {
    activity_state::setTracing(trace);
    for (size_t i = 0; i < std::size(diversion_sites::kSets); ++i) {
        if (!g_matched[i]) continue;
        const diversion_sites::Set& set = diversion_sites::kSets[i];
        const site_installer::Result result = site_installer::install(set.sites, &bindingOf);
        if (!result.installed) {
            logger::write("diversions: set %s not patched", set.name);
        } else {
            logger::write("diversions: set %s patched (%zu rows)", set.name, result.patched);
        }
    }
}

}  // namespace diversion_patches
