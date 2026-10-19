#include "world_walks.h"

#include "log.h"
#include "site_installer.h"
#include "world_walks_sites.h"
#include "world_walks_thunks.h"

namespace {

void installSet(const world_walks_sites::Set& set, bool activities) {
    if (set.needsActivities && !activities) {
        logger::write("world walks: %s left out, the activity logic is not installed", set.name);
        return;
    }
    if (!site_installer::sitesMatch(set.sites)) {
        logger::write("world walks: %s sites differ, not patched", set.name);
        return;
    }
    const site_installer::Result result = site_installer::install(set.sites, false, &world_walks::bindingOf);
    if (result.installed) {
        logger::write("world walks: %s, %zu sites patched", set.name, result.patched);
    } else {
        logger::write("world walks: %s not patched", set.name);
    }
}

}  // namespace

namespace world_walks {

partner_thunks::Binding bindingOf(partner_sites::Thunk thunk) {
    for (const auto bind : {&world_walks_thunks::bind, &partner_thunks::bindCalls}) {
        const partner_thunks::Binding binding = bind(thunk);
        if (binding.entry) return binding;
    }
    return {nullptr, nullptr};
}

void install(bool trace, bool activities) {
    world_walks_thunks::setTrace(trace);
    for (const world_walks_sites::Set& set : world_walks_sites::kSets) installSet(set, activities);
}

}  // namespace world_walks
