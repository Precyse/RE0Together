#include "ds2/world_facts.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <vector>

#include "ds2/engine.h"
#include "ds2/player.h"
#include "ds2/player_state.h"
#include "game.h"
#include "hooks.h"
#include "log.h"
#include "paths.h"
#include "remote_apply.h"

namespace {

// The exported setters forward to these writers, which the engine's own code also calls directly:
// (database, const GUID* fact UUID, const value*, const byte* default, persistence flag, persistence flag,
// bool* changed). Both are thread-safe: they lock one of 32 stripes chosen from the database address.
constexpr uintptr_t kWriteBool = 0x1401499c0;
constexpr uintptr_t kWriteInt = 0x14064d080;
constexpr size_t kGuidSize = fact_wire::kUuidSize;
constexpr int kMaxLogged = 4000;
constexpr int64_t kCheckIntervalMs = 500;
constexpr int64_t kClockIntervalMs = 250;  // how often the writers re-check that gameplay has started
constexpr size_t kMaxQueued = 4096;
constexpr size_t kMaxKnown = 16384;  // distinct facts remembered for a join snapshot

using WriteFn = uint64_t (*)(uintptr_t database, const uint8_t* uuid, const void* value, const void* previous,
                            uintptr_t flag5, uintptr_t flag6, uint8_t* changed, uintptr_t a8);

WriteFn g_writeBool = nullptr;
WriteFn g_writeInt = nullptr;
std::atomic<bool> g_log{false};
std::atomic<int> g_logged{0};
std::atomic<uint64_t> g_calls{0};

std::atomic<uintptr_t> g_database{0};  // seen as the first argument of every write
std::atomic<bool> g_share{false};
ds2::GameplayClock g_clock;
std::atomic<int64_t> g_clockAt{0};
std::mutex g_queueMutex;
std::vector<fact_wire::Entry> g_queue;
using FactKey = std::array<uint8_t, kGuidSize + 1>;  // the kind, then the UUID
std::map<FactKey, fact_wire::Entry> g_known;  // the last queued value of every fact, kept for join snapshots

void hex(const uint8_t* bytes, char* out) {
    for (size_t i = 0; i < kGuidSize; ++i) sprintf_s(out + i * 2, 3, "%02x", bytes[i]);
}

// Logging is switched on while the file <game>\coop\facts_on.txt exists (checked twice a second): the loading burst of
// thousands of writes would otherwise fill the log before the action under study.
bool logging() {
    static std::atomic<int64_t> checkedAt{0};
    static std::atomic<bool> on{false};
    const int64_t now = GetTickCount64();
    if (now - checkedAt.load() > kCheckIntervalMs) {
        checkedAt = now;
        on = GetFileAttributesW((coopDirectory() + L"\\facts_on.txt").c_str()) != INVALID_FILE_ATTRIBUTES;
        if (on) logger::write("world_facts: %llu writes seen so far", static_cast<unsigned long long>(g_calls.load()));
    }
    return on.load();
}

void record(const char* kind, const uint8_t* uuid, double value) {
    ++g_calls;
    if (!g_log.load() || !logging() || g_logged.fetch_add(1) >= kMaxLogged) return;
    char uuidHex[kGuidSize * 2 + 1] = {};
    uint8_t zero[kGuidSize] = {};
    hex(uuid ? uuid : zero, uuidHex);
    logger::write("world_facts: %s fact %s = %g", kind, uuidHex, value);
}

// Whether the world is in gameplay, not loading or on the title screen (checked a few times a second).
bool gameplayRunning() {
    const int64_t now = GetTickCount64();
    if (now - g_clockAt.load() > kClockIntervalMs) {
        g_clockAt = now;
        g_clock.update(ds2::localPlayerEntity());
    }
    return g_clock.settled();
}

// Queues a change for the guests: one entry per fact, the last value.
void queue(uint8_t kind, const uint8_t* uuid, uint32_t value, uintptr_t flag5, uintptr_t flag6) {
    if (!g_share.load() || remote_apply::active() || !static_cast<uint8_t>(flag5) || !gameplayRunning()) return;  // only Persistent facts
    fact_wire::Entry entry{};
    entry.kind = kind;
    entry.flags = (static_cast<uint8_t>(flag5) ? fact_wire::kFlagArg5 : 0) | (static_cast<uint8_t>(flag6) ? fact_wire::kFlagArg6 : 0);
    std::memcpy(entry.uuid, uuid, kGuidSize);
    entry.value = value;
    FactKey key{kind};
    std::memcpy(key.data() + 1, uuid, kGuidSize);
    std::lock_guard lock(g_queueMutex);
    if (g_known.contains(key) || g_known.size() < kMaxKnown) g_known[key] = entry;
    for (fact_wire::Entry& queued : g_queue) {
        if (queued.kind == kind && std::memcmp(queued.uuid, uuid, kGuidSize) == 0) {
            queued = entry;
            return;
        }
    }
    if (g_queue.size() < kMaxQueued) g_queue.push_back(entry);
}

// Calls the original writer, then reports the write if it changed the fact.
uint64_t write(WriteFn original, uint8_t kind, uintptr_t database, const uint8_t* uuid, const void* value,
               const void* previous, uintptr_t flag5, uintptr_t flag6, uint8_t* changed, uintptr_t a8) {
    g_database = database;
    const uint64_t result = original(database, uuid, value, previous, flag5, flag6, changed, a8);
    if (changed && *changed) {
        uint32_t bits = 0;
        std::memcpy(&bits, value, kind == fact_wire::kKindBool ? sizeof(uint8_t) : sizeof(uint32_t));
        queue(kind, uuid, bits, flag5, flag6);
    }
    return result;
}

uint64_t writeBoolDetour(uintptr_t database, const uint8_t* uuid, const void* value, const void* previous,
                         uintptr_t flag5, uintptr_t flag6, uint8_t* changed, uintptr_t a8) {
    record("bool", uuid, *static_cast<const uint8_t*>(value));
    return write(g_writeBool, fact_wire::kKindBool, database, uuid, value, previous, flag5, flag6, changed, a8);
}

uint64_t writeIntDetour(uintptr_t database, const uint8_t* uuid, const void* value, const void* previous,
                        uintptr_t flag5, uintptr_t flag6, uint8_t* changed, uintptr_t a8) {
    record("int", uuid, *static_cast<const int32_t*>(value));
    return write(g_writeInt, fact_wire::kKindInt, database, uuid, value, previous, flag5, flag6, changed, a8);
}

}  // namespace

namespace world_facts {

void installEarly(bool log) {
    g_log = log;
    hooks::install("fact write bool", ds2::at(kWriteBool), reinterpret_cast<void*>(&writeBoolDetour),
                   reinterpret_cast<void**>(&g_writeBool));
    hooks::install("fact write int", ds2::at(kWriteInt), reinterpret_cast<void*>(&writeIntDetour),
                   reinterpret_cast<void**>(&g_writeInt));
}

}  // namespace world_facts

namespace game {

void shareFactWrites(bool on) {
    if (g_share.exchange(on) && !on) {
        std::lock_guard lock(g_queueMutex);
        g_queue.clear();
        g_known.clear();
    }
}

std::vector<fact_wire::Entry> takeFactWrites() {
    std::lock_guard lock(g_queueMutex);
    std::vector<fact_wire::Entry> out;
    out.swap(g_queue);
    return out;
}

std::vector<fact_wire::Entry> factSnapshot() {
    std::lock_guard lock(g_queueMutex);
    std::vector<fact_wire::Entry> out;
    out.reserve(g_known.size());
    for (const auto& [key, entry] : g_known) out.push_back(entry);
    return out;
}

bool gameplaySettled() { return gameplayRunning(); }

bool applyFact(const fact_wire::Entry& fact) {
    const uintptr_t database = g_database.load();
    WriteFn writer = fact.kind == fact_wire::kKindBool ? g_writeBool : g_writeInt;
    if (!database || !writer) return false;
    // `previous` differs from the new value so a fact the database does not hold yet counts as changed.
    uint32_t value = fact.value;
    uint32_t previous = ~fact.value;
    uint8_t changed = 0;
    const remote_apply::Scope applying;
    writer(database, fact.uuid, &value, &previous, (fact.flags & fact_wire::kFlagArg5) != 0,
           (fact.flags & fact_wire::kFlagArg6) != 0, &changed, 0);
    return true;
}

}  // namespace game
