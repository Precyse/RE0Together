#include "activity_patches.h"

#include "activity_sites.h"
#include "activity_state.h"
#include "activity_thunks.h"
#include "diversion_thunks.h"
#include "log.h"
#include "partner_thunks.h"
#include "script_override.h"
#include "site_installer.h"

namespace {

const partner_sites::Set kPhases[] = {
    {"1", activity_sites::kPhase1},
    {"2", activity_sites::kPhase2},
    {"3", activity_sites::kPhase3},
};

// The identity and nearest thunks of milestone 4 serve the same call patterns as the new ones; the Snatch carry count of
// AC46 is milestone 16 wave 2's.
partner_thunks::Binding bindingOf(partner_sites::Thunk thunk) {
    for (const auto bind : {&activity_thunks::bindFramework, &activity_thunks::bindCredit, &activity_thunks::bindGames,
                            &partner_thunks::bindCalls, &diversion_thunks::bind}) {
        const partner_thunks::Binding binding = bind(thunk);
        if (binding.entry) return binding;
    }
    return {nullptr, nullptr};
}

}  // namespace

namespace activity_patches {

bool sitesMatch() {
    bool ok = true;
    size_t total = 0;
    for (const partner_sites::Set& phase : kPhases) {
        ok = site_installer::sitesMatch(phase.sites) && ok;
        total += phase.sites.size();
    }
    if (ok) logger::write("activities: %zu sites match", total);
    return ok;
}

bool install(bool trace) {
    activity_state::setTracing(trace);
    logger::write("activities: %zu script overrides in coop\\scripts", script_override::names().size());
    bool all = true;
    for (const partner_sites::Set& phase : kPhases) {
        const site_installer::Result result = site_installer::install(phase.sites, &bindingOf);
        if (result.installed) {
            logger::write("activities: phase %s, %zu sites patched", phase.name, result.patched);
        } else {
            logger::write("activities: phase %s not patched", phase.name);
        }
        all = all && result.installed;
    }
    return all;
}

}  // namespace activity_patches
