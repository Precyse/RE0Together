#include "activity_patches.h"

#include <span>

#include "activity_sites.h"
#include "activity_state.h"
#include "activity_thunks.h"
#include "log.h"
#include "partner_thunks.h"
#include "site_installer.h"

namespace {

struct Phase {
    const char* name;
    std::span<const partner_sites::Site> sites;
};

const Phase kPhases[] = {
    {"1", activity_sites::kPhase1},
};

// The identity and nearest thunks of milestone 4 serve the same call patterns as the new ones.
partner_thunks::Binding bindingOf(partner_sites::Thunk thunk) {
    for (const auto bind : {&activity_thunks::bindFramework, &activity_thunks::bindCredit, &activity_thunks::bindGames,
                            &partner_thunks::bindCalls}) {
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
    for (const Phase& phase : kPhases) {
        ok = site_installer::sitesMatch(phase.sites) && ok;
        total += phase.sites.size();
    }
    if (ok) logger::write("activities: %zu sites match", total);
    return ok;
}

bool install(bool experimental, bool trace) {
    activity_state::setTracing(trace);
    bool all = true;
    for (const Phase& phase : kPhases) {
        const site_installer::Result result = site_installer::install(phase.sites, experimental, &bindingOf);
        if (result.installed) {
            logger::write("activities: phase %s, %zu sites patched, %zu gated (probe E, probe H)", phase.name,
                          result.patched, result.gated);
        } else {
            logger::write("activities: phase %s not patched", phase.name);
        }
        all = all && result.installed;
    }
    return all;
}

}  // namespace activity_patches
