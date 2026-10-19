#include "fuzz_patches.h"

#include <windows.h>

#include <array>
#include <iterator>
#include <span>

#include "activity_state.h"
#include "diversion_thunks.h"
#include "fuzz_sites.h"
#include "fuzz_thunks.h"
#include "log.h"
#include "party_ai_thunks.h"
#include "site_installer.h"

namespace {

// Which sets matched the exe at start-up.
std::array<bool, std::size(fuzz_sites::kSets)> g_matched{};

// The address of the first row of `sites` whose bytes differ, 0 when all match.
uintptr_t firstDifference(std::span<const partner_sites::Site> sites) {
    for (const partner_sites::Site& site : sites) {
        if (!site_installer::sitesMatch(std::span<const partner_sites::Site>(&site, 1))) return site.address;
    }
    return 0;
}

}  // namespace

namespace fuzz_patches {

partner_thunks::Binding bindingOf(partner_sites::Thunk thunk) {
    for (const auto bind : {&fuzz_thunks::bind, &diversion_thunks::bind, &party_ai_thunks::bind}) {
        const partner_thunks::Binding binding = bind(thunk);
        if (binding.entry) return binding;
    }
    return {nullptr, nullptr};
}

bool sitesMatch() {
    bool any = false;
    for (size_t i = 0; i < std::size(fuzz_sites::kSets); ++i) {
        const fuzz_sites::Set& set = fuzz_sites::kSets[i];
        const uintptr_t differs = firstDifference(set.sites);
        g_matched[i] = differs == 0;
        any = any || g_matched[i];
        if (differs) {
            logger::write("fuzz: set %s differs at 0x%X, not installed", set.name, static_cast<unsigned>(differs));
        } else {
            logger::write("fuzz: set %s matches (%zu rows)", set.name, set.sites.size());
        }
    }
    return any;
}

void install(bool trace) {
    activity_state::setTracing(trace);
    fuzz_thunks::seed(GetTickCount());
    for (size_t i = 0; i < std::size(fuzz_sites::kSets); ++i) {
        if (!g_matched[i]) continue;
        const fuzz_sites::Set& set = fuzz_sites::kSets[i];
        const site_installer::Result result = site_installer::install(set.sites, &bindingOf);
        if (result.installed) {
            logger::write("fuzz: set %s patched (%zu rows)", set.name, result.patched);
        } else {
            logger::write("fuzz: set %s not patched", set.name);
        }
    }
}

}  // namespace fuzz_patches
