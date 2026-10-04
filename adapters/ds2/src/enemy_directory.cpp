#include "enemy_directory.h"

#include <map>
#include <mutex>

namespace {

std::mutex g_mutex;
std::map<uint16_t, enemy_directory::Uuid> g_byNetId;
std::map<enemy_directory::Uuid, uint16_t> g_byUuid;

void eraseLocked(uint16_t netId) {
    const auto found = g_byNetId.find(netId);
    if (found == g_byNetId.end()) return;
    g_byUuid.erase(found->second);
    g_byNetId.erase(found);
}

}  // namespace

namespace enemy_directory {

void set(uint16_t netId, const Uuid& uuid) {
    std::lock_guard lock(g_mutex);
    eraseLocked(netId);
    if (const auto owner = g_byUuid.find(uuid); owner != g_byUuid.end()) g_byNetId.erase(owner->second);
    g_byNetId[netId] = uuid;
    g_byUuid[uuid] = netId;
}

void forget(uint16_t netId) {
    std::lock_guard lock(g_mutex);
    eraseLocked(netId);
}

void clear() {
    std::lock_guard lock(g_mutex);
    g_byNetId.clear();
    g_byUuid.clear();
}

uint16_t netIdOf(const Uuid& uuid) {
    std::lock_guard lock(g_mutex);
    const auto found = g_byUuid.find(uuid);
    return found == g_byUuid.end() ? 0 : found->second;
}

std::vector<Entry> all() {
    std::lock_guard lock(g_mutex);
    std::vector<Entry> entries;
    entries.reserve(g_byNetId.size());
    for (const auto& [netId, uuid] : g_byNetId) entries.push_back({netId, uuid});
    return entries;
}

}  // namespace enemy_directory
