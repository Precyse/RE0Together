#include "multiplayer_patches.h"

#include "creation_queue.h"
#include "cutscene_sync.h"
#include "join_admission.h"
#include "log.h"
#include "mission_flow.h"
#include "multiplayer_sites.h"
#include "multiplayer_thunks.h"
#include "net_clock.h"
#include "pause_mask.h"
#include "player_visuals.h"
#include "population.h"
#include "sender_scope.h"
#include "site_installer.h"
#include "vehicle_pool.h"

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

void installScriptTraces() {
    size_t installed = 0;
    size_t total = 0;
    for (const auto table : multiplayer_sites::kScriptTraces) {
        for (const partner_sites::Site& site : table) {
            ++total;
            const std::span<const partner_sites::Site> one(&site, 1);
            if (!site_installer::sitesMatch(one)) {
                logger::write("trace: %s not installed, the bytes differ", site.id);
                continue;
            }
            if (site_installer::install(one, false, &multiplayer_thunks::bindingOf).installed) ++installed;
        }
    }
    logger::write("trace: cutscene end, Lua error and fade traces, %zu of %zu sites installed", installed, total);
}

bool install(bool trace, bool leavePathInstalled, bool experimental) {
    cutscene_sync::setTrace(trace);
    sender_scope::setTrace(trace);
    creation_queue::setTrace(trace);
    join_admission::setTrace(trace);
    mission_flow::setTrace(trace);
    net_clock::setTrace(trace);
    pause_mask::setTrace(trace);
    population::setTrace(trace);
    player_visuals::setTrace(trace);
    vehicle_pool::setTrace(trace);

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