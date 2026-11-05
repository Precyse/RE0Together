#include "calls.h"

#include "log.h"
#include "refw.h"

namespace {

constexpr const char* kFeatureNames[] = {"body", "clock", "offline", "serializer", "puppet", "saves", "profile", "enemy"};
static_assert(std::size(kFeatureNames) == static_cast<size_t>(calls::Feature::Count));

std::array<REFrameworkMethodHandle, calls::kCount> g_methods{};
std::array<bool, static_cast<size_t>(calls::Feature::Count)> g_enabled{};

size_t index(calls::Feature feature) { return static_cast<size_t>(feature); }

// A feature that also calls another feature's rows is off when that one is.
struct Dependency {
    calls::Feature feature, needs;
};
constexpr Dependency kDependencies[] = {{calls::Feature::Puppet, calls::Feature::Serializer},
                                        {calls::Feature::Profile, calls::Feature::Serializer},
                                        {calls::Feature::Enemy, calls::Feature::Puppet}};

}  // namespace

namespace calls {

void resolve() {
    g_enabled.fill(true);
    for (const Row& row : kRows) {
        const REFrameworkMethodHandle method = refw::findMethod(row.type, row.method, row.params);
        const uintptr_t body = refw::rebasedBody(method);
        if (method && body == row.pinned) {
            g_methods[row.id] = method;
            continue;
        }
        g_enabled[index(row.feature)] = false;
        logger::write("calls: %s.%s(%s) %s (body 0x%llX, pinned 0x%llX)", row.type, row.method, row.params,
                      method ? "moved" : "not found", static_cast<unsigned long long>(body),
                      static_cast<unsigned long long>(row.pinned));
    }
    for (const Dependency& dependency : kDependencies) {
        if (!g_enabled[index(dependency.needs)]) g_enabled[index(dependency.feature)] = false;
    }
    for (size_t i = 0; i < g_enabled.size(); ++i) {
        logger::write("calls: %s %s", kFeatureNames[i], g_enabled[i] ? "on" : "off");
    }
}

bool enabled(Feature feature) { return g_enabled[index(feature)]; }

REFrameworkMethodHandle method(Id id) { return enabled(kRows[id].feature) ? g_methods[id] : nullptr; }

void* objectResult(Id id, void* self, std::initializer_list<void*> args) {
    const refw::InvokeResult* result = refw::invoke(method(id), self, args);
    return result ? result->ptr : nullptr;
}

}  // namespace calls
