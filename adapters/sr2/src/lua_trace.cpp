#include "lua_trace.h"

#include <windows.h>

#include <mutex>
#include <string>

#include "fpu_guard.h"
#include "log.h"
#include "lua_trace_rules.h"
#include "vehicle_seating_rules.h"

namespace {

std::mutex g_limitMutex;
script_trace_rules::NamedRateLimit g_lineLimit;

}  // namespace

namespace lua_trace {

void log(const char* text, size_t length) {
    if (vehicle_seating_rules::isSeatTrace(text)) {
        logger::write("trace: lua: %s", text);
        return;
    }
    FpuGuard guard;
    const std::string line = lua_trace_rules::coopLine(text, length);
    if (line.empty()) return;
    uint32_t triggers = 0;
    {
        std::lock_guard<std::mutex> lock(g_limitMutex);
        if (!g_lineLimit.allow(lua_trace_rules::rateKey(line), GetTickCount64(), triggers)) return;
    }
    logger::write("trace: lua: %s", line.c_str());
}

}  // namespace lua_trace
