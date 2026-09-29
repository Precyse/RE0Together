#include "player_damage.h"

#include <windows.h>

#include <mutex>
#include <vector>

#include "character_owner.h"
#include "debug_stats.h"
#include "game.h"
#include "game_tick.h"
#include "hooks.h"
#include "log.h"

namespace {

using SetHpFunction = void(__fastcall*)(void* self, void* edx, int32_t hp);
using OnDeathFunction = void(__fastcall*)(void* think, void* edx, void* player);

constexpr size_t kMaxQueuedDeaths = 4;

SetHpFunction g_originalSetHp = nullptr;
OnDeathFunction g_originalOnDeath = nullptr;
NetClient* g_net = nullptr;
thread_local bool t_authoritative = false;  // set while the adapter itself applies HP or a death
std::mutex g_mutex;
std::vector<uint8_t> g_pendingDeaths;  // character ids, guarded by g_mutex

bool isRemotePlayer(uintptr_t object) {
    return character_owner::isRemoteOwned(character_owner::identify(object));
}

struct Authoritative {
    Authoritative() { t_authoritative = true; }
    ~Authoritative() { t_authoritative = false; }
};

// Every damage path (normal hits, queued damage, the lethal hit) ends in setHP, so this is the single gate:
// only the owner's machine changes a player's HP.
void __fastcall setHpDetour(void* self, void* edx, int32_t hp) {
    if (!t_authoritative && isRemotePlayer(reinterpret_cast<uintptr_t>(self))) return;
    g_originalSetHp(self, edx, hp);
}

// cPlayerThink death handler (vtable slot 28). A remote-owned character only dies when its owner reports it.
void __fastcall onDeathDetour(void* think, void* edx, void* player) {
    const uintptr_t object = reinterpret_cast<uintptr_t>(player);
    const auto character = character_owner::identify(object);
    if (!t_authoritative && character_owner::isRemoteOwned(character)) return;
    g_originalOnDeath(think, edx, player);
    if (!t_authoritative && character_owner::isLocalOwned(character) && g_net) {
        const uint8_t id = static_cast<uint8_t>(character);
        g_net->send(player_damage::kMsgPlayerDied, true, proto::kSlotAll, {&id, sizeof(id)});
        debug_stats::count(debug_stats::Counter::DeathsReported);
        logger::write("player_damage: local character %u died, reported", id);
    }
}

void applyPendingDeaths() {
    std::vector<uint8_t> batch;
    {
        std::lock_guard lock(g_mutex);
        batch.swap(g_pendingDeaths);
    }
    for (const uint8_t id : batch) {
        const auto character = static_cast<character_owner::Character>(id);
        const uintptr_t player = character_owner::find(character);
        const uintptr_t think = player ? game::readPointer(player + game::kPlayerThinkOffset) : 0;
        if (!think || !character_owner::isRemoteOwned(character)) continue;
        Authoritative scope;
        g_originalOnDeath(reinterpret_cast<void*>(think), nullptr, reinterpret_cast<void*>(player));
        debug_stats::count(debug_stats::Counter::DeathsApplied);
        logger::write("player_damage: remote character %u died", id);
    }
}

}  // namespace

namespace player_damage {

bool install(NetClient& net) {
    g_net = &net;
    game_tick::addCallback("player_deaths", applyPendingDeaths);
    return hooks::install("setHP", game::kSetHpFunction, reinterpret_cast<void*>(&setHpDetour),
                          reinterpret_cast<void**>(&g_originalSetHp)) &&
           hooks::install("cPlayerThink::onDeath", game::kPlayerOnDeathFunction, reinterpret_cast<void*>(&onDeathDetour),
                          reinterpret_cast<void**>(&g_originalOnDeath));
}

void uninstall() {
    hooks::remove(game::kSetHpFunction);
    hooks::remove(game::kPlayerOnDeathFunction);
}

void setHp(uintptr_t character, int32_t hp) {
    if (!g_originalSetHp) return;
    Authoritative scope;
    g_originalSetHp(reinterpret_cast<void*>(character), nullptr, hp);
}

void onFrame(const GameFrame& frame) {
    if (frame.type != kMsgPlayerDied || frame.payload.size() != 1) return;
    std::lock_guard lock(g_mutex);
    if (g_pendingDeaths.size() < kMaxQueuedDeaths) g_pendingDeaths.push_back(frame.payload[0]);
}

}  // namespace player_damage
