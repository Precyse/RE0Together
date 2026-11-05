#pragma once
// Hook rows (pure, unit tested): rule H1 (the resolved body must be the pinned one) and H2 (a shared body is never
// hooked) from the DD2 design, adapter.md 3.
#include <algorithm>
#include <cstdint>
#include <span>

#include "names.h"

namespace hook_rule {

enum class Verdict : uint8_t { Accept, NotFound, Moved, Shared };

// `resolved` is the method's native body rebased to the dump's image base (0 when the TDB has no such method).
inline Verdict decide(const names::MethodRow& row, uintptr_t resolved, std::span<const uintptr_t> sharedBodies) {
    if (!resolved) return Verdict::NotFound;
    if (row.share != 1 || std::find(sharedBodies.begin(), sharedBodies.end(), resolved) != sharedBodies.end()) {
        return Verdict::Shared;
    }
    return resolved == row.pinned ? Verdict::Accept : Verdict::Moved;
}

inline const char* name(Verdict verdict) {
    switch (verdict) {
        case Verdict::Accept: return "ok";
        case Verdict::NotFound: return "not in the type database";
        case Verdict::Moved: return "body moved (another build?)";
        case Verdict::Shared: return "shared body";
    }
    return "?";
}

}  // namespace hook_rule
