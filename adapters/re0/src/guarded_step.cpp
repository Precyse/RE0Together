#include "guarded_step.h"

#include <windows.h>

#include <exception>

#include "log.h"

namespace {

bool runCatching(const char* name, void (*step)()) {
    try {
        step();
        return true;
    } catch (const std::exception& error) {
        logger::write("%s: exception '%s', state reset", name, error.what());
    } catch (...) {
        logger::write("%s: unknown exception, state reset", name);
    }
    return false;
}

bool runProtected(const char* name, void (*step)()) {
    __try {
        return runCatching(name, step);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        logger::write("%s: fault 0x%08lx, state reset", name, GetExceptionCode());
        return false;
    }
}

}  // namespace

bool guardedStep(const char* name, void (*step)(), void (*reset)()) {
    if (runProtected(name, step)) return true;
    reset();
    return false;
}
