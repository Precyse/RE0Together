#include "roster.h"

#include <windows.h>

#include <algorithm>
#include <cstring>
#include <map>
#include <mutex>
#include <optional>
#include <vector>

#include "calls.h"
#include "hook_registry.h"
#include "log.h"
#include "names.h"
#include "refw.h"
#include "roster_rule.h"
#include "session.h"
#include "wire_dd2.h"
#include "world_gate.h"

namespace {

// REFramework hook arguments: argv[0] the VM context, argv[1] `this`, then the method's parameters, 8 bytes each.
constexpr int kFirstParam = 2;
constexpr uint32_t kHolderUniqueId = 0x80;      // ContextHolder.UniqueIDContext
constexpr uint32_t kContextUniqueId = 0x18;     // UniqueIDContext.UniqueID (Guid, then Int32 index)
constexpr uint32_t kUniqueIdIndex = 0x10;       // UniqueID._Index inside the value
constexpr uint64_t kSettleMs = 3000;
constexpr int kResolveTries = 90;  // 3 s of steps before a component whose key never reads is dropped

struct Pending {
    void* component = nullptr;
    bool monster = false;
    int tries = 0;
};

std::mutex g_mutex;
std::map<uintptr_t, roster::Member> g_own;  // by component; under g_mutex
std::vector<Pending> g_pending;             // registered, key not read yet; under g_mutex
uint64_t g_changes = 0;                     // under g_mutex
uint32_t g_unreadable = 0;                  // game thread: components dropped since the last line

uint64_t g_seenChanges = 0, g_changedAtMs = 0;
std::vector<uint8_t> g_servedPeers;  // host: the peers that have the roster last sent
uint64_t g_sentChanges = UINT64_MAX;
std::optional<wire_dd2::Roster> g_host;  // guest: the host's newest roster
bool g_comparePending = false;           // guest: either roster changed since the last comparison
std::optional<roster_rule::Comparison> g_lastComparison;

// An enemy component's character and its UniqueID, by field reads and the character's ContextHolder; false while a link is
// missing or a read faults (a component the engine has not finished, or one already gone). Game thread; no objects with
// destructors here, for __try.
bool readMember(void* component, bool monster, void** chara, roster_rule::Key* key) {
    __try {
        *chara = calls::objectResult(monster ? calls::kGetMonsterChara : calls::kGetEnemyChara, component);
        void* holder = *chara ? calls::objectResult(calls::kGetContext, *chara) : nullptr;
        void* context = holder ? refw::field<void*>(holder, kHolderUniqueId) : nullptr;
        if (!context) return false;
        const auto* value = static_cast<const uint8_t*>(context) + kContextUniqueId;
        std::memcpy(key->rowId.data(), value, key->rowId.size());
        std::memcpy(&key->index, value + kUniqueIdIndex, sizeof(key->index));
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// The hooks only note the component: it is read on the game thread once the engine has finished it (resolvePending).
void added(void** argv, int argc, bool monster) {
    void* component = argc > kFirstParam ? argv[kFirstParam] : nullptr;
    if (!component) return;
    std::lock_guard lock(g_mutex);
    g_pending.push_back({component, monster, 0});
}

void removed(void** argv, int argc) {
    if (argc <= kFirstParam) return;
    void* component = argv[kFirstParam];
    std::lock_guard lock(g_mutex);
    std::erase_if(g_pending, [component](const Pending& pending) { return pending.component == component; });
    if (g_own.erase(reinterpret_cast<uintptr_t>(component))) ++g_changes;
}

// Game thread: reads the keys of the components registered since the last step; one that never reads is dropped after 3 s.
void resolvePending() {
    std::vector<Pending> pending;
    {
        std::lock_guard lock(g_mutex);
        pending.swap(g_pending);
    }
    std::vector<Pending> retry;
    std::vector<roster::Member> resolved;
    for (Pending& each : pending) {
        roster::Member member{{}, each.component, nullptr, each.monster};
        if (readMember(each.component, each.monster, &member.character, &member.key)) {
            resolved.push_back(member);
        } else if (++each.tries < kResolveTries) {
            retry.push_back(each);
        } else {
            ++g_unreadable;
        }
    }
    std::lock_guard lock(g_mutex);
    for (const roster::Member& member : resolved) g_own[reinterpret_cast<uintptr_t>(member.component)] = member;
    if (!resolved.empty()) ++g_changes;
    g_pending.insert(g_pending.end(), retry.begin(), retry.end());
}

int preRegisterEnemy(int argc, void** argv, REFrameworkTypeDefinitionHandle*, unsigned long long) {
    added(argv, argc, false);
    return REFRAMEWORK_HOOK_CALL_ORIGINAL;
}

int preRegisterMonster(int argc, void** argv, REFrameworkTypeDefinitionHandle*, unsigned long long) {
    added(argv, argc, true);
    return REFRAMEWORK_HOOK_CALL_ORIGINAL;
}

int preRemove(int argc, void** argv, REFrameworkTypeDefinitionHandle*, unsigned long long) {
    removed(argv, argc);
    return REFRAMEWORK_HOOK_CALL_ORIGINAL;
}

std::vector<roster_rule::Key> ownKeys(uint64_t& changes) {
    std::lock_guard lock(g_mutex);
    changes = g_changes;
    std::vector<roster_rule::Key> out;
    out.reserve(g_own.size());
    for (const auto& [component, member] : g_own) out.push_back(member.key);
    return out;
}

void sendHost(const std::vector<roster_rule::Key>& keys, uint64_t changes, const std::vector<uint8_t>& present) {
    const bool joined = std::any_of(present.begin(), present.end(), [](uint8_t slot) {
        return std::find(g_servedPeers.begin(), g_servedPeers.end(), slot) == g_servedPeers.end();
    });
    if (changes == g_sentChanges && !joined) return;
    const size_t count = std::min(keys.size(), size_t{wire_dd2::kMaxRosterKeys});
    if (session::send(wire_dd2::kEnemyRoster, true, wire_dd2::encodeRoster(world_gate::loadSerial(), {keys.data(), count}))) {
        g_sentChanges = changes;
        g_servedPeers = present;
    }
}

void compareGuest(const std::vector<roster_rule::Key>& keys) {
    if (!g_host || !g_comparePending) return;
    g_comparePending = false;
    roster_rule::Comparison now = roster_rule::compare(g_host->keys, keys);
    if (g_lastComparison && *g_lastComparison == now) return;
    logger::write("roster: host %zu (load %u), own %zu: %zu equal, %zu extra here, %zu missing here", g_host->keys.size(),
                  g_host->header.loadSerial, keys.size(), now.both.size(), now.extra.size(), now.missing.size());
    g_lastComparison = std::move(now);
}

}  // namespace

namespace roster {

bool install() {
    if (!calls::enabled(calls::Feature::Body)) {
        logger::write("%s: not hooked (body calls missing)", names::kRoster);
        return false;
    }
    const hook_registry::Handler handlers[] = {
        hook_registry::make<preRegisterEnemy>(&names::kRegisterEnemy),
        hook_registry::make<preRemove>(&names::kRemoveEnemy),
        hook_registry::make<preRegisterMonster>(&names::kRegisterMonster),
        hook_registry::make<preRemove>(&names::kRemoveMonster),
    };
    return hook_registry::installSet(names::kRoster, handlers);
}

void tick(uint64_t nowMs) {
    resolvePending();
    if (g_unreadable) {
        logger::write("roster: %u registered enemies never gave a key, left out", g_unreadable);
        g_unreadable = 0;
    }
    uint64_t changes = 0;
    const std::vector<roster_rule::Key> keys = ownKeys(changes);
    const bool ownChanged = changes != g_seenChanges;
    if (ownChanged) {
        g_seenChanges = changes;
        g_changedAtMs = nowMs;
        g_comparePending = true;
    }
    const std::vector<uint8_t> present = session::matchingPeers();
    if (present.empty() || world_gate::place() != world_gate_rule::Place::InGame || nowMs - g_changedAtMs < kSettleMs) return;
    if (session::isHost()) {
        sendHost(keys, changes, present);
    } else {
        compareGuest(keys);
    }
}

std::vector<Member> members() {
    std::lock_guard lock(g_mutex);
    std::vector<Member> out;
    out.reserve(g_own.size());
    for (const auto& [component, member] : g_own) out.push_back(member);
    return out;
}

const std::vector<roster_rule::Key>* hostKeys() { return g_host ? &g_host->keys : nullptr; }

bool settled(uint64_t nowMs) { return nowMs - g_changedAtMs >= kSettleMs; }

bool onFrame(uint8_t, std::span<const uint8_t> payload) {
    auto decoded = wire_dd2::decodeRoster(payload);
    if (!decoded) return false;
    g_host = std::move(decoded);
    g_comparePending = true;
    return true;
}

}  // namespace roster
