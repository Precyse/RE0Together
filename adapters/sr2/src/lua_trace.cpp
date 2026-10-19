#include "lua_trace.h"

#include <windows.h>

#include <mutex>
#include <span>
#include <string>

#include "fpu_guard.h"
#include "log.h"
#include "lua_trace_rules.h"
#include "lua_trace_sites.h"
#include "memory.h"
#include "site_installer.h"
#include "sr2_addresses.h"

namespace {

using partner_sites::Thunk;

constexpr size_t kReadLimit = lua_trace_rules::kCoopPrefixLength + lua_trace_rules::kLineLimit + 1;

using MissionDebugFunction = int(__cdecl*)(uint32_t lua);

void* g_missionDebugOriginal = nullptr;
std::mutex g_limitMutex;
script_trace_rules::NamedRateLimit g_lineLimit;

// The text of the first argument of the running native, empty when there is none or it is not a string. Lua 5.0 in this exe: the
// arguments start at L->base, 16 byte objects with the type tag at +0 and the value at +8; a string value points to a TString
// whose characters start at +0x10.
std::string firstStringArgument(uint32_t lua) {
    const uint32_t top = readDword(lua + sr2::kLuaTopOffset);
    const uint32_t base = readDword(lua + sr2::kLuaBaseOffset);
    if (!base || top < base + sr2::kLuaObjectSize) return {};
    if (readDword(base + sr2::kLuaObjectTypeOffset) != sr2::kLuaTypeString) return {};
    const uint32_t string = readDword(base + sr2::kLuaObjectValueOffset);
    if (!string) return {};
    char text[kReadLimit + 1];
    const size_t length = readCString(string + sr2::kLuaStringCharsOffset, text, sizeof(text));
    return lua_trace_rules::coopLine(text, length);
}

void logCoopLine(uint32_t lua) {
    FpuGuard guard;
    const std::string line = firstStringArgument(lua);
    if (line.empty()) return;
    uint32_t triggers = 0;
    {
        std::lock_guard<std::mutex> lock(g_limitMutex);
        if (!g_lineLimit.allow(lua_trace_rules::rateKey(line), GetTickCount64(), triggers)) return;
    }
    logger::write("trace: lua: %s", line.c_str());
}

// LT1: mission_debug(L). The game's version returns the number 0 and ignores the text.
int __cdecl traceLuaDebug(uint32_t lua) {
    logCoopLine(lua);
    return reinterpret_cast<MissionDebugFunction>(g_missionDebugOriginal)(lua);
}

}  // namespace

namespace lua_trace {

partner_thunks::Binding bind(partner_sites::Thunk thunk) {
    if (thunk != Thunk::TraceLuaDebug) return {nullptr, nullptr};
    return {reinterpret_cast<void*>(&traceLuaDebug), &g_missionDebugOriginal};
}

void install() {
    const std::span<const partner_sites::Site> sites(lua_trace_sites::kSites);
    if (!site_installer::sitesMatch(sites)) {
        logger::write("trace: LT1 not installed, the bytes differ");
        return;
    }
    if (!site_installer::install(sites, false, &bind).installed) {
        logger::write("trace: LT1 not installed");
        return;
    }
    logger::write("trace: lua: mission_debug hook installed (LT1)");
}

}  // namespace lua_trace
