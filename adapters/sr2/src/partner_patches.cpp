#include "partner_patches.h"

#include "log.h"
#include "partner_scope.h"
#include "partner_sites.h"
#include "partner_thunks.h"
#include "site_installer.h"

namespace {

partner_thunks::Binding bindingOf(partner_sites::Thunk thunk) {
    for (const auto bind : {&partner_thunks::bindCalls, &partner_thunks::bindSession, &partner_thunks::bindGameplay}) {
        const partner_thunks::Binding binding = bind(thunk);
        if (binding.entry) return binding;
    }
    return {nullptr, nullptr};
}

}  // namespace

namespace partner_patches {

bool sitesMatch() {
    const bool ok = site_installer::sitesMatch(partner_sites::kSites);
    if (ok) logger::write("partner: %zu sites match", partner_sites::kSiteCount);
    return ok;
}

bool install(bool experimental, bool trace, uint32_t maxPlayers) {
    if (trace) partner_scope::enableTrace(maxPlayers);
    const site_installer::Result result = site_installer::install(partner_sites::kSites, experimental, &bindingOf);
    if (!result.installed) return false;
    if (experimental) {
        logger::write("partner: %zu sites patched, the experimental ones included", partner_sites::kSiteCount);
    } else {
        logger::write("partner: %zu sites patched, %zu gated (probe G)", result.patched, result.gated);
    }
    return true;
}

}  // namespace partner_patches
