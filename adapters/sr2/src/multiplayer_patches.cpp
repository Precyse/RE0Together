#include "multiplayer_patches.h"

#include "creation_queue.h"
#include "join_admission.h"
#include "log.h"
#include "multiplayer_sites.h"
#include "multiplayer_thunks.h"
#include "net_clock.h"
#include "pause_mask.h"
#include "sender_scope.h"
#include "site_installer.h"

namespace multiplayer_patches {

bool sitesMatch() {
    bool ok = true;
    size_t total = 0;
    for (const multiplayer_sites::Phase& phase : multiplayer_sites::kPhases) {
        ok = site_installer::sitesMatch(phase.sites) && ok;
        total += phase.sites.size();
    }
    if (ok) logger::write("players 3 and 4: %zu sites match", total);
    return ok;
}

bool install(bool trace, bool leavePathInstalled, bool experimental) {
    sender_scope::setTrace(trace);
    creation_queue::setTrace(trace);
    join_admission::setTrace(trace);
    net_clock::setTrace(trace);
    pause_mask::setTrace(trace);

    bool all = true;
    for (const multiplayer_sites::Phase& phase : multiplayer_sites::kPhases) {
        if (phase.traceOnly && !trace) continue;
        if (phase.needsLeavePath && !leavePathInstalled) {
            logger::write("players 3 and 4: %s not patched, the member leave edits are not in", phase.name);
            all = false;
            continue;
        }

        const site_installer::Result result = site_installer::install(phase.sites, experimental, &multiplayer_thunks::bindingOf);
        if (result.installed) {
            logger::write("players 3 and 4: %s, %zu sites patched, %zu waiting for a probe", phase.name, result.patched, result.gated);
        } else {
            logger::write("players 3 and 4: %s not patched", phase.name);
        }
        all = all && result.installed;
    }
    return all;
}

}  // namespace multiplayer_patches