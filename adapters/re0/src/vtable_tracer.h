#pragma once
#include <vector>

#include "config.h"

namespace vtable_tracer {

// Replaces every slot of each vtable with a counting thunk and logs the changed slots every 2 s.
bool install(const std::vector<VtableTrace>& vtables);

// Restores the original vtable entries and stops the reporter.
void uninstall();

}  // namespace vtable_tracer
