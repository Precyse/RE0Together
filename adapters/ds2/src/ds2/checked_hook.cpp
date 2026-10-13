#include "ds2/checked_hook.h"

#include "ds2/engine.h"
#include "hooks.h"
#include "log.h"
#include "pattern_scan.h"

namespace checked_hook {

bool install(const char* name, uintptr_t fileVa, const char* expected, void* detour, void** original) {
    if (!pattern_scan::matches(ds2::at(fileVa), expected)) {
        logger::write("%s: not hooked, the code at %llx is not what this adapter was written for", name,
                      static_cast<unsigned long long>(fileVa));
        return false;
    }
    return hooks::install(name, ds2::at(fileVa), detour, original);
}

}  // namespace checked_hook
