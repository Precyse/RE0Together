#include "revive_patches.h"

#include <span>

#include "log.h"
#include "notoriety_thunks.h"
#include "partner_thunks.h"
#include "revive_sites.h"
#include "revive_state.h"
#include "revive_thunks.h"
#include "site_installer.h"

namespace {

using partner_sites::Site;
using partner_sites::Thunk;

// The code of a row: the revive thunks, the notoriety thunks, or the shared call replacement RK1 reuses.
partner_thunks::Binding bindingOf(Thunk thunk) {
    for (const partner_thunks::Binding binding : {revive_thunks::bind(thunk), notoriety_thunks::bind(thunk), partner_thunks::bindCalls(thunk)}) {
        if (binding.entry) return binding;
    }
    return {nullptr, nullptr};
}

// Checks and writes one set; true when it went in.
bool installSet(std::span<const Site> sites, const char* name) {
    if (!site_installer::sitesMatch(sites)) {
        logger::write("revive: %s sites differ, not installed", name);
        return false;
    }
    const site_installer::Result result = site_installer::install(sites, false, &bindingOf);
    if (!result.installed) {
        logger::write("revive: %s not installed", name);
        return false;
    }
    logger::write("revive: %s, %zu sites patched", name, result.patched);
    return true;
}

}  // namespace

namespace revive_patches {

void install(bool trace) {
    revive_state::setTrace(trace);
    notoriety_thunks::setTrace(trace);
    if (installSet(revive_sites::kRevive, "downed and revive")) revive_state::setInstalled(true);
    installSet(revive_sites::kNotoriety, "notoriety");
}

}  // namespace revive_patches
