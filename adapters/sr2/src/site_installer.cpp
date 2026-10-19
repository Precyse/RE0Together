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

// The function pointer a table entry holds before the swap, for the thunk to call on.
void* oldPointer(const Site& site) {
    uintptr_t pointer = 0;
    const std::vector<uint8_t> bytes = partner_sites::parseHex(site.expected);
    for (size_t i = 0; i < bytes.size() && i < sizeof(uint32_t); ++i) pointer |= static_cast<uintptr_t>(bytes[i]) << (8 * i);
    return reinterpret_cast<void*>(pointer);
}

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

uintptr_t firstDifference(std::span<const Site> sites) {
    for (const Site& site : sites) {
        if (!sitesMatch(std::span<const Site>(&site, 1))) return site.address;
    }
    return 0;
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
        if (site.kind == Kind::Bytes) {
            patches.push_back(patchFor(site, 0));
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
            if (site.kind == Kind::PointerSwap && binding.original) *binding.original = oldPointer(site);
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
