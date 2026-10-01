#include "main_thread.h"

#include <windows.h>

#include <atomic>
#include <chrono>
#include <map>
#include <mutex>

#include "hooks.h"
#include "log.h"

namespace {

using FrameFn = uintptr_t (*)(uintptr_t);

constexpr auto kCensusTime = std::chrono::seconds(3);

FrameFn g_original = nullptr;
main_thread::Callback g_tick = nullptr;
std::atomic<DWORD> g_mainThread{0};
thread_local bool t_inTick = false;

std::mutex g_censusMutex;
std::map<DWORD, unsigned> g_census;  // calls per thread until the main thread is chosen
std::chrono::steady_clock::time_point g_censusStart;

// Counts callers until kCensusTime has passed, then fixes the busiest thread as the main thread.
void census() {
    std::lock_guard lock(g_censusMutex);
    if (g_mainThread.load()) return;
    const auto now = std::chrono::steady_clock::now();
    if (g_census.empty()) g_censusStart = now;
    ++g_census[GetCurrentThreadId()];
    if (now - g_censusStart < kCensusTime) return;
    DWORD busiest = 0;
    unsigned most = 0;
    for (const auto& [thread, calls] : g_census) {
        logger::write("main_thread: thread %lu called the frame function %u times", thread, calls);
        if (calls > most) busiest = thread, most = calls;
    }
    g_mainThread = busiest;
    logger::write("main_thread: simulation thread %lu", busiest);
}

uintptr_t frameDetour(uintptr_t argument) {
    if (!g_mainThread.load()) census();
    if (!t_inTick && g_mainThread.load() == GetCurrentThreadId()) {
        t_inTick = true;
        g_tick();
        t_inTick = false;
    }
    return g_original(argument);
}

}  // namespace

namespace main_thread {

bool install(uintptr_t frameFunction, Callback tick) {
    g_tick = tick;
    return frameFunction && hooks::install("frame function", frameFunction, reinterpret_cast<void*>(&frameDetour),
                                           reinterpret_cast<void**>(&g_original));
}

}  // namespace main_thread
