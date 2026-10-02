#include "ds2/world_facts.h"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "ds2/engine.h"
#include "hooks.h"
#include "log.h"
#include "paths.h"

namespace {

// The exported setters forward to these writers, which the engine's own code also calls directly:
// (database, const GUID* fact UUID, const value*, ...) with more arguments on the stack.
constexpr uintptr_t kWriteBool = 0x1401499c0;
constexpr uintptr_t kWriteInt = 0x14064d080;
constexpr size_t kGuidSize = 16;
constexpr int kMaxLogged = 4000;
constexpr int64_t kCheckIntervalMs = 500;

using WriteFn = uint64_t (*)(uintptr_t database, const uint8_t* uuid, const void* value, uintptr_t a4, uintptr_t a5,
                            uintptr_t a6, uintptr_t a7, uintptr_t a8);

WriteFn g_writeBool = nullptr;
WriteFn g_writeInt = nullptr;
std::atomic<bool> g_log{false};
std::atomic<int> g_logged{0};
std::atomic<uint64_t> g_calls{0};

void hex(const uint8_t* bytes, char* out) {
    for (size_t i = 0; i < kGuidSize; ++i) sprintf_s(out + i * 2, 3, "%02x", bytes[i]);
}

// Logging is switched on while the file <game>\coopacts_on.txt exists (checked twice a second): the loading burst of
// thousands of writes would otherwise fill the log before the action under study.
bool logging() {
    static std::atomic<int64_t> checkedAt{0};
    static std::atomic<bool> on{false};
    const int64_t now = GetTickCount64();
    if (now - checkedAt.load() > kCheckIntervalMs) {
        checkedAt = now;
        on = GetFileAttributesW((coopDirectory() + L"\facts_on.txt").c_str()) != INVALID_FILE_ATTRIBUTES;
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

uint64_t writeBoolDetour(uintptr_t database, const uint8_t* uuid, const void* value, uintptr_t a4, uintptr_t a5,
                         uintptr_t a6, uintptr_t a7, uintptr_t a8) {
    record("bool", uuid, *static_cast<const uint8_t*>(value));
    return g_writeBool(database, uuid, value, a4, a5, a6, a7, a8);
}

uint64_t writeIntDetour(uintptr_t database, const uint8_t* uuid, const void* value, uintptr_t a4, uintptr_t a5,
                        uintptr_t a6, uintptr_t a7, uintptr_t a8) {
    record("int", uuid, *static_cast<const int32_t*>(value));
    return g_writeInt(database, uuid, value, a4, a5, a6, a7, a8);
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
