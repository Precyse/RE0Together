#include "party_ai_patches.h"

#include "log.h"
#include "party_ai_sites.h"
#include "party_ai_thunks.h"
#include "site_installer.h"

namespace party_ai_patches {

void install(bool trace) {
    party_ai_thunks::setTrace(trace);
    for (const party_ai_sites::Phase& phase : party_ai_sites::kPhases) {
        if (phase.traceOnly && !trace) continue;
        if (!site_installer::sitesMatch(phase.sites)) {
            logger::write("party ai: %s sites differ, not installed", phase.name);
            continue;
        }
        const site_installer::Result result = site_installer::install(phase.sites, false, &party_ai_thunks::bind);
        if (result.installed) {
            logger::write("party ai: %s, %zu sites patched", phase.name, result.patched);
        } else {
            logger::write("party ai: %s not patched", phase.name);
        }
    }
}

}  // namespace party_ai_patches
