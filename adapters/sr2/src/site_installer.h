#pragma once
#include <cstddef>
#include <span>

#include "partner_sites.h"
#include "partner_thunks.h"

// Writes a table of exe sites (partner_sites::Site) the way milestones 4 and 7 do: every byte checked, the code edits
// all or none, the hooks queued after them.
namespace site_installer {

// The code behind a site's thunk: {nullptr, nullptr} when the binder does not know it.
using Binder = partner_thunks::Binding (*)(partner_sites::Thunk);

// Checks the bytes of every site (the gated ones included) and logs each difference. True when all match.
bool sitesMatch(std::span<const partner_sites::Site> sites);

// The address of the first site whose bytes differ, 0 when all match. Logs each difference, as sitesMatch does.
uintptr_t firstDifference(std::span<const partner_sites::Site> sites);

struct Result {
    bool installed;
    size_t patched;
    size_t gated;  // left out because they wait for a live probe
};

// Writes the sites that are not gated (or all of them when `experimental`). Nothing is written when a thunk is
// missing or a site differs; the hooks are queued after the code edits.
Result install(std::span<const partner_sites::Site> sites, bool experimental, Binder binder);

}  // namespace site_installer
