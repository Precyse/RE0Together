#include "site_installer.h"

#include <vector>

#include "code_patch.h"
#include "hooks.h"
#include "log.h"
#include "partner_replacement.h"

namespace {

using partner_sites::Kind;
using partner_sites::Site;

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

bool matchSets(const char* label, std::span<const partner_sites::Set> sets, std::span<bool> matched) {
    bool any = false;
    for (size_t i = 0; i < sets.size(); ++i) {
        const partner_sites::Set& set = sets[i];
        const uintptr_t differs = firstDifference(set.sites);
        matched[i] = differs == 0;
        any = any || matched[i];
        if (differs) {
            logger::write("%s: set %s differs at 0x%X, not installed", label, set.name, static_cast<unsigned>(differs));
        } else {
            logger::write("%s: set %s matches (%zu rows)", label, set.name, set.sites.size());
        }
    }
    return any;
}

void installMatchedSets(const char* label, std::span<const partner_sites::Set> sets, std::span<const bool> matched, Binder binder) {
    for (size_t i = 0; i < sets.size(); ++i) {
        if (!matched[i]) continue;
        const Result result = install(sets[i].sites, binder);
        if (result.installed) {
            logger::write("%s: set %s patched (%zu rows)", label, sets[i].name, result.patched);
        } else {
            logger::write("%s: set %s not patched", label, sets[i].name);
        }
    }
}

Result install(std::span<const Site> sites, Binder binder) {
    std::vector<CodePatch> patches;
    std::vector<const Site*> hookSites;
    for (const Site& site : sites) {
        if (site.kind == Kind::Bytes) {
            patches.push_back(patchFor(site, 0));
            continue;
        }
        const partner_thunks::Binding binding = binder(site.thunk);
        if (!binding.entry) {
            logger::write("sites: no thunk for %s, nothing patched", site.id);
            return {false, 0};
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
        return {false, 0};
    }
    for (const Site* site : hookSites) {
        const partner_thunks::Binding binding = binder(site->thunk);
        hooks::install(site->id, site->address, binding.entry, binding.original);
    }
    return {true, sites.size()};
}

}  // namespace site_installer
