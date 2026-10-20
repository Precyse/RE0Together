#include "crowd_control_patches.h"

#include <array>
#include <iterator>

#include "activity_state.h"
#include "crowd_control_sites.h"
#include "crowd_control_thunks.h"
#include "diversion_thunks.h"
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

bool sitesMatch() { return site_installer::matchSets("crowd control", crowd_control_sites::kSets, g_matched); }

void install(bool trace) {
    activity_state::setTracing(trace);
    site_installer::installMatchedSets("crowd control", crowd_control_sites::kSets, g_matched, &bindingOf);
}

}  // namespace crowd_control_patches
