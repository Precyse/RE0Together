#include "multiplayer_patches.h"

#include "creation_queue.h"
#include "log.h"
#include "multiplayer_sites.h"
#include "multiplayer_thunks.h"
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

bool install(bool trace) {
    sender_scope::setTrace(trace);
    creation_queue::setTrace(trace);

    bool all = true;
    for (const multiplayer_sites::Phase& phase : multiplayer_sites::kPhases) {
        if (phase.traceOnly && !trace) continue;
        const site_installer::Result result = site_installer::install(phase.sites, false, &multiplayer_thunks::bindingOf);
        if (result.installed) {
            logger::write("players 3 and 4: %s, %zu sites patched", phase.name, result.patched);
        } else {
            logger::write("players 3 and 4: %s not patched", phase.name);
        }
        all = all && result.installed;
    }
    return all;
}

}  // namespace multiplayer_patches