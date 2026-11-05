#include "hook_registry.h"

#include <vector>

#include "hook_rule.h"
#include "log.h"
#include "refw.h"

namespace {

// Stand-ins for a handler a row leaves out, so REFramework never gets a null callback.
int passPre(int, void**, REFrameworkTypeDefinitionHandle*, unsigned long long) { return REFRAMEWORK_HOOK_CALL_ORIGINAL; }
void passPost(void**, REFrameworkTypeDefinitionHandle, unsigned long long) {}

}  // namespace

namespace hook_registry {

bool installSet(const char* set, std::span<const Handler> handlers) {
    std::vector<REFrameworkMethodHandle> methods;
    for (const Handler& handler : handlers) {
        const names::MethodRow& row = *handler.row;
        const REFrameworkMethodHandle method = refw::findMethod(row.type, row.method, row.params);
        const uintptr_t body = refw::rebasedBody(method);
        const hook_rule::Verdict verdict = hook_rule::decide(row, body, names::kSharedBodies);
        if (verdict != hook_rule::Verdict::Accept) {
            logger::write("%s: not hooked (%s: %s, body 0x%llX, pinned 0x%llX)", set, row.id, hook_rule::name(verdict),
                          static_cast<unsigned long long>(body), static_cast<unsigned long long>(row.pinned));
            return false;
        }
        methods.push_back(method);
    }
    std::vector<unsigned> ids;
    for (size_t i = 0; i < handlers.size(); ++i) {
        const unsigned id = refw::addHook(methods[i], handlers[i].pre ? handlers[i].pre : passPre,
                                           handlers[i].post ? handlers[i].post : passPost);
        if (!id) {
            for (size_t done = 0; done < ids.size(); ++done) refw::removeHook(methods[done], ids[done]);
            logger::write("%s: not hooked (%s: REFramework refused the hook)", set, handlers[i].row->id);
            return false;
        }
        ids.push_back(id);
    }
    logger::write("%s: %zu hooked", set, handlers.size());
    return true;
}

}  // namespace hook_registry
