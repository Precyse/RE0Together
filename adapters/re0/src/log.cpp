#include "log.h"

#include <windows.h>

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <string>

#include "paths.h"

namespace {

constexpr size_t kLineCapacity = 1024;
constexpr auto kRepeatWindow = std::chrono::seconds(1);

std::mutex g_mutex;
std::mutex g_repeatMutex;  // guards the last repeat-filtered line
std::string g_lastFiltered;
std::chrono::steady_clock::time_point g_lastFilteredTime;

void append(const char* message) {
    SYSTEMTIME t;
    GetLocalTime(&t);

    std::lock_guard lock(g_mutex);
    const std::wstring path = coopDirectory() + L"\\adapter.log";
    FILE* file = _wfsopen(path.c_str(), L"a", _SH_DENYNO);
    if (!file) return;
    fprintf(file, "%04u-%02u-%02u %02u:%02u:%02u.%03u %s\n", t.wYear, t.wMonth, t.wDay, t.wHour,
            t.wMinute, t.wSecond, t.wMilliseconds, message);
    fclose(file);
}

}  // namespace

namespace logger {

void write(const char* format, ...) {
    char message[kLineCapacity];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    append(message);
}

void writeUnlessRepeated(const char* format, ...) {
    char message[kLineCapacity];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    {
        std::lock_guard lock(g_repeatMutex);
        const auto now = std::chrono::steady_clock::now();
        if (message == g_lastFiltered && now - g_lastFilteredTime < kRepeatWindow) return;
        g_lastFiltered = message;
        g_lastFilteredTime = now;
    }
    append(message);
}

}  // namespace logger
