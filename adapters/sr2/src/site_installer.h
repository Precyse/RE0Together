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

// Checks the bytes of every site and logs each difference. True when all match.
bool sitesMatch(std::span<const partner_sites::Site> sites);

// The address of the first site whose bytes differ, 0 when all match. Logs each difference, as sitesMatch does.
uintptr_t firstDifference(std::span<const partner_sites::Site> sites);

struct Result {
    bool installed;
    size_t patched;
};

// Checks each set of a table against the exe and records in `matched` which agree; logs each as "<label>: set <name> ...".
// True when at least one set matches. `matched` has one entry per set.
bool matchSets(const char* label, std::span<const partner_sites::Set> sets, std::span<bool> matched);

// Writes each set that `matchSets` found matching, on its own: a set that cannot be written leaves the others alone.
void installMatchedSets(const char* label, std::span<const partner_sites::Set> sets, std::span<const bool> matched, Binder binder);

// Writes every site. Nothing is written when a thunk is missing or a site differs; the hooks are queued after the code edits, and a hook
// that cannot be installed takes the whole set back (the hooks already installed are removed, the code edits reverted).
Result install(std::span<const partner_sites::Site> sites, Binder binder);

}  // namespace site_installer
