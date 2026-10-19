#include "hud_patches.h"

#include "hud_remotes.h"
#include "hud_sites.h"
#include "log.h"
#include "site_installer.h"
#include "waypoint_scope.h"

namespace {

partner_thunks::Binding bindingOf(partner_sites::Thunk thunk) {
    for (const auto bind : {&hud_remotes::bind, &waypoint_scope::bind, &partner_thunks::bindCalls}) {
        const partner_thunks::Binding binding = bind(thunk);
        if (binding.entry) return binding;
    }
    return {nullptr, nullptr};
}

void installSet(const hud_sites::Set& set) {
    if (!site_installer::sitesMatch(set.sites)) {
        logger::write("hud: %s sites differ, not patched", set.name);
        return;
    }
    const site_installer::Result result = site_installer::install(set.sites, false, &bindingOf);
    if (result.installed) {
        logger::write("hud: %s, %u sites patched", set.name, static_cast<unsigned>(result.patched));
    } else {
        logger::write("hud: %s not patched", set.name);
    }
}

}  // namespace

namespace hud_patches {

void install(bool trace) {
    hud_remotes::setTrace(trace);
    waypoint_scope::setTrace(trace);
    for (const hud_sites::Set& set : hud_sites::kSets) {
        installSet(set);
    }
}

}  // namespace hud_patches
