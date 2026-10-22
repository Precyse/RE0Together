#include "fuzz_patches.h"

#include <windows.h>

#include <array>
#include <iterator>

#include "activity_state.h"
#include "diversion_thunks.h"
#include "fuzz_sites.h"
#include "fuzz_thunks.h"
#include "party_ai_thunks.h"
#include "site_installer.h"

namespace {

// Which sets matched the exe at start-up.
std::array<bool, std::size(fuzz_sites::kSets)> g_matched{};

}  // namespace

namespace fuzz_patches {

partner_thunks::Binding bindingOf(partner_sites::Thunk thunk) {
    for (const auto bind : {&fuzz_thunks::bind, &diversion_thunks::bind, &party_ai_thunks::bind}) {
        const partner_thunks::Binding binding = bind(thunk);
        if (binding.entry) return binding;
    }
    return {nullptr, nullptr};
}

bool sitesMatch() { return site_installer::matchSets("fuzz", fuzz_sites::kSets, g_matched); }

void install(bool trace) {
    activity_state::setTracing(trace);
    fuzz_thunks::seed(GetTickCount());
    site_installer::installMatchedSets("fuzz", fuzz_sites::kSets, g_matched, &bindingOf);
}

}  // namespace fuzz_patches
