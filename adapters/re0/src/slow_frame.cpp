#include "slow_frame.h"

#include <windows.h>

#include <atomic>

#include "log.h"

namespace {

constexpr uint64_t kNotTiming = UINT64_MAX;
constexpr uint64_t kUsPerSecond = 1'000'000;

std::atomic<bool> g_enabled{false};
slow_frame::Tracker g_tracker;  // game thread only

uint64_t nowUs() {
    static const uint64_t frequency = [] {
        LARGE_INTEGER value;
        QueryPerformanceFrequency(&value);
        return static_cast<uint64_t>(value.QuadPart);
    }();
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    const uint64_t ticks = static_cast<uint64_t>(counter.QuadPart);
    return ticks / frequency * kUsPerSecond + ticks % frequency * kUsPerSecond / frequency;
}

}  // namespace

namespace slow_frame {

void enable() { g_enabled = true; }

void beginFrame() {
    if (!g_enabled.load(std::memory_order_relaxed) || !g_tracker.beginFrame(nowUs())) return;
    const Report& report = g_tracker.report();
    logger::write("SLOW_FRAME %ums: net %u puppet %u enemies %u events %u fx %u other %u", report.frameMs,
                  report.categoryMs[static_cast<size_t>(Category::Net)], report.categoryMs[static_cast<size_t>(Category::Puppet)],
                  report.categoryMs[static_cast<size_t>(Category::Enemies)], report.categoryMs[static_cast<size_t>(Category::Events)],
                  report.categoryMs[static_cast<size_t>(Category::Fx)], report.categoryMs[static_cast<size_t>(Category::Other)]);
}

Scope::Scope(Category category)
    : category_(category), startUs_(g_enabled.load(std::memory_order_relaxed) ? nowUs() : kNotTiming) {}

Scope::~Scope() {
    if (startUs_ != kNotTiming) g_tracker.add(category_, nowUs() - startUs_);
}

}  // namespace slow_frame
