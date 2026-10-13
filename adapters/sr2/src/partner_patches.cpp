#include "partner_patches.h"

#include <vector>

#include "code_patch.h"
#include "hooks.h"
#include "log.h"
#include "partner_replacement.h"
#include "partner_scope.h"
#include "partner_sites.h"
#include "partner_thunks.h"

namespace {

using partner_sites::Gate;
using partner_sites::Kind;
using partner_sites::Site;
using partner_replacement::replacementFor;

partner_thunks::Binding bindingOf(partner_sites::Thunk thunk) {
    for (const auto bind : {&partner_thunks::bindCalls, &partner_thunks::bindSession, &partner_thunks::bindGameplay}) {
        const partner_thunks::Binding binding = bind(thunk);
        if (binding.entry) return binding;
    }
    return {nullptr, nullptr};
}

bool isPatched(const Site& site, bool experimental) { return site.gate == Gate::Now || experimental; }

CodePatch patchFor(const Site& site, uintptr_t thunk) {
    return {site.id, site.address, partner_sites::parseHex(site.expected), replacementFor(site, thunk)};
}

}  // namespace

namespace partner_patches {

bool sitesMatch() {
    std::vector<CodePatch> checks;
    for (const Site& site : partner_sites::kSites) {
        checks.push_back({site.id, site.address, partner_sites::parseHex(site.expected), {}});
    }
    const bool ok = codeMatches(checks);
    if (ok) logger::write("partner: %zu sites match", partner_sites::kSiteCount);
    return ok;
}

bool install(bool experimental, bool trace, uint32_t maxPlayers) {
    std::vector<CodePatch> patches;
    std::vector<const Site*> hookSites;
    size_t gated = 0;
    for (const Site& site : partner_sites::kSites) {
        if (!isPatched(site, experimental)) {
            ++gated;
            continue;
        }
        const partner_thunks::Binding binding = bindingOf(site.thunk);
        if (!binding.entry) {
            logger::write("partner: no thunk for %s, nothing patched", site.id);
            return false;
        }
        if (site.kind == Kind::HookEntry) {
            hookSites.push_back(&site);
        } else {
            patches.push_back(patchFor(site, reinterpret_cast<uintptr_t>(binding.entry)));
        }
    }
    if (!applyCodePatches(patches)) {
        logger::write("partner: code edits not applied, nothing patched");
        return false;
    }
    if (trace) partner_scope::enableTrace(maxPlayers);
    for (const Site* site : hookSites) {
        const partner_thunks::Binding binding = bindingOf(site->thunk);
        hooks::install(site->id, site->address, binding.entry, binding.original);
    }
    if (experimental) {
        logger::write("partner: %zu sites patched, the experimental ones included", partner_sites::kSiteCount);
    } else {
        logger::write("partner: %zu sites patched, %zu gated (probe B, probe G)", partner_sites::kSiteCount - gated, gated);
    }
    return true;
}

}  // namespace partner_patches
