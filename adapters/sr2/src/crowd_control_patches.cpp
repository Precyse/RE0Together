#include "crowd_control_patches.h"

#include <array>
#include <iterator>

#include "activity_state.h"
#include "crowd_control_sites.h"
#include "crowd_control_thunks.h"
#include "diversion_thunks.h"
#include "log.h"
#include "partner_thunks.h"
#include "site_installer.h"

namespace {

// Which sets matched the exe at start-up.
std::array<bool, std::size(crowd_control_sites::kSets)> g_matched{};

// CR12 retargets the followers call to the Trafficking thunk that dismisses the followers of every remote (DVK2).
partner_thunks::Binding bindingOf(partner_sites::Thunk thunk) {
    const partner_thunks::Binding own = crowd_control_thunks::bind(thunk);
    return own.entry ? own : diversion_thunks::bind(thunk);
}

}  // namespace

namespace crowd_control_patches {

bool sitesMatch() {
    bool any = false;
    for (size_t i = 0; i < std::size(crowd_control_sites::kSets); ++i) {
        const crowd_control_sites::Set& set = crowd_control_sites::kSets[i];
        const uintptr_t differs = site_installer::firstDifference(set.sites);
        g_matched[i] = differs == 0;
        any = any || g_matched[i];
        if (differs) {
            logger::write("crowd control: set %s differs at 0x%X, not installed", set.name, static_cast<unsigned>(differs));
        } else {
            logger::write("crowd control: set %s matches (%zu rows)", set.name, set.sites.size());
        }
    }
    return any;
}

void install(bool trace) {
    activity_state::setTracing(trace);
    for (size_t i = 0; i < std::size(crowd_control_sites::kSets); ++i) {
        if (!g_matched[i]) continue;
        const crowd_control_sites::Set& set = crowd_control_sites::kSets[i];
        const site_installer::Result result = site_installer::install(set.sites, false, &bindingOf);
        if (result.installed) {
            logger::write("crowd control: set %s patched (%zu rows)", set.name, result.patched);
        } else {
            logger::write("crowd control: set %s not patched", set.name);
        }
    }
}

}  // namespace crowd_control_patches
