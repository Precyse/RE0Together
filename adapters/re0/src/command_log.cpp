#include "command_log.h"

#include <cstdarg>
#include <cstdio>

#include "debug_stats.h"
#include "log.h"

namespace {

constexpr size_t kTextCapacity = 160;

}  // namespace

namespace command_log {

void press(const char* name, const char* source) {
    logger::writeUnlessRepeated("command: %s pressed (%s)", name, source);
    debug_stats::setLastCommand(name);
}

void note(const char* format, ...) {
    char text[kTextCapacity];
    va_list args;
    va_start(args, format);
    vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    logger::writeUnlessRepeated("command: %s", text);
}

void decide(const char* format, ...) {
    char text[kTextCapacity];
    va_list args;
    va_start(args, format);
    vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    logger::writeUnlessRepeated("command: decision: %s", text);
    debug_stats::setLastDecision(text);
}

}  // namespace command_log
