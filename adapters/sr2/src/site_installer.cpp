#include "site_installer.h"

#include <vector>

#include "code_patch.h"
#include "hooks.h"
#include "log.h"
#include "partner_replacement.h"

namespace {

using partner_sites::Gate;
using partner_sites::Kind;
using partner_sites::Site;

bool isPatched(const Site& site, bool experimental) { return site.gate == Gate::Now || experimental; }

CodePatch patchFor(const Site& site, uintptr_t thunk) {
    return {site.id, site.address, partner_sites::parseHex(site.expected), partner_replacement::replacementFor(site, thunk)};
}

}  // namespace

namespace site_installer {

bool sitesMatch(std::span<const Site> sites) {
    std::vector<CodePatch> checks;
    for (const Site& site : sites) checks.push_back({site.id, site.address, partner_sites::parseHex(site.expected), {}});
    return codeMatches(checks);
}

Result install(std::span<const Site> sites, bool experimental, Binder binder) {
    std::vector<CodePatch> patches;
    std::vector<const Site*> hookSites;
    size_t gated = 0;
    for (const Site& site : sites) {
        if (!isPatched(site, experimental)) {
            ++gated;
            continue;
        }
        const partner_thunks::Binding binding = binder(site.thunk);
        if (!binding.entry) {
            logger::write("sites: no thunk for %s, nothing patched", site.id);
            return {false, 0, gated};
        }
        if (site.kind == Kind::HookEntry) {
            hookSites.push_back(&site);
        } else {
            patches.push_back(patchFor(site, reinterpret_cast<uintptr_t>(binding.entry)));
        }
    }
    if (!applyCodePatches(patches)) {
        logger::write("sites: code edits not applied, nothing patched");
        return {false, 0, gated};
    }
    for (const Site* site : hookSites) {
        const partner_thunks::Binding binding = binder(site->thunk);
        hooks::install(site->id, site->address, binding.entry, binding.original);
    }
    return {true, sites.size() - gated, gated};
}

}  // namespace site_installer
