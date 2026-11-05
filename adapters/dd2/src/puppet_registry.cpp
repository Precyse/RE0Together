#include "puppet_registry.h"

#include <windows.h>

#include <atomic>
#include <map>
#include <set>

#include "calls.h"
#include "log.h"
#include "object_set.h"
#include "refw.h"

namespace {

struct Entry {
    std::optional<puppet_registry::Body> body;
    bool destroyed = false;  // the engine removed the body itself
};

SRWLOCK g_lock = SRWLOCK_INIT;
std::map<uint32_t, Entry> g_entries;  // under g_lock
ObjectSet g_objects;                  // every bound body's character and controllers
std::atomic<size_t> g_count{0};       // gates skip the lock while no puppet exists
thread_local int t_driverDepth = 0;

void* objectOf(calls::Id id, void* character) { return calls::objectResult(id, character); }

// Under g_lock.
void rebuildObjects() {
    std::set<const void*> objects;
    for (const auto& [id, entry] : g_entries) {
        if (!entry.body) continue;
        for (const void* object : {entry.body->character, entry.body->actions, entry.body->selector, entry.body->navigation,
                                   entry.body->hit, entry.body->turn}) {
            if (object) objects.insert(object);
        }
    }
    g_objects.replace(std::move(objects));
}

}  // namespace

namespace puppet_registry {

void expect(uint32_t characterId) {
    AcquireSRWLockExclusive(&g_lock);
    g_entries[characterId] = {};
    g_count = g_entries.size();
    ReleaseSRWLockExclusive(&g_lock);
}

void forget(uint32_t characterId) {
    AcquireSRWLockExclusive(&g_lock);
    g_entries.erase(characterId);
    rebuildObjects();
    g_count = g_entries.size();
    ReleaseSRWLockExclusive(&g_lock);
}

std::optional<Body> body(uint32_t characterId) {
    AcquireSRWLockShared(&g_lock);
    const auto it = g_entries.find(characterId);
    std::optional<Body> out = it == g_entries.end() ? std::nullopt : it->second.body;
    ReleaseSRWLockShared(&g_lock);
    return out;
}

void onReady(void* character) {
    if (!g_count.load(std::memory_order_relaxed) || !character) return;
    // Copied at once: every invoke below reuses the same result storage.
    const refw::InvokeResult* result = refw::invoke(calls::method(calls::kGetCharaID), character);
    if (!result) return;
    const uint32_t id = result->dword;
    AcquireSRWLockShared(&g_lock);
    const bool ours = g_entries.count(id) != 0;
    ReleaseSRWLockShared(&g_lock);
    if (!ours) return;
    Body bound{character,
               objectOf(calls::kGetActionManager, character),
               objectOf(calls::kGetHumanActionSelector, character),
               objectOf(calls::kGetNavigationController, character),
               objectOf(calls::kGetHit, character),
               objectOf(calls::kGetTargetAngleCtrl, character)};
    AcquireSRWLockExclusive(&g_lock);
    if (auto it = g_entries.find(id); it != g_entries.end()) it->second = {bound, false};
    rebuildObjects();
    ReleaseSRWLockExclusive(&g_lock);
    logger::write("puppet_registry: body of 0x%08X ready (actions %d, selector %d, navigation %d, hit %d, turn %d)", id,
                  bound.actions != nullptr, bound.selector != nullptr, bound.navigation != nullptr, bound.hit != nullptr,
                  bound.turn != nullptr);
}

void onDestroyed(void* character) {
    if (!g_count.load(std::memory_order_relaxed)) return;
    AcquireSRWLockExclusive(&g_lock);
    for (auto& [id, entry] : g_entries) {
        if (entry.body && entry.body->character == character) {
            entry.body.reset();
            entry.destroyed = true;
            logger::write("puppet_registry: body of 0x%08X destroyed", id);
        }
    }
    rebuildObjects();
    ReleaseSRWLockExclusive(&g_lock);
}

bool destroyed(uint32_t characterId) {
    AcquireSRWLockShared(&g_lock);
    const auto it = g_entries.find(characterId);
    const bool gone = it != g_entries.end() && it->second.destroyed;
    ReleaseSRWLockShared(&g_lock);
    return gone;
}

bool isPuppetId(uint32_t characterId) {
    if (!g_count.load(std::memory_order_relaxed)) return false;
    AcquireSRWLockShared(&g_lock);
    const bool ours = g_entries.count(characterId) != 0;
    ReleaseSRWLockShared(&g_lock);
    return ours;
}

bool isPuppetObject(const void* object) { return g_objects.contains(object); }

DriverScope::DriverScope() { ++t_driverDepth; }
DriverScope::~DriverScope() { --t_driverDepth; }
bool inDriverScope() { return t_driverDepth > 0; }

}  // namespace puppet_registry
