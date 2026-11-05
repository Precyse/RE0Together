#include "capture.h"

#include <windows.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <vector>

#include "build_gate.h"
#include "log.h"
#include "paths.h"

namespace {

constexpr size_t kBufferLimit = 16u << 20;  // bytes held between two writer passes
constexpr DWORD kWriterPeriodMs = 250;

FILE* g_file = nullptr;
std::mutex g_mutex;
std::vector<uint8_t> g_pending;  // under g_mutex
std::atomic<uint64_t> g_dropped;

DWORD WINAPI writerThread(LPVOID) {
    std::vector<uint8_t> batch;
    for (;;) {
        Sleep(kWriterPeriodMs);
        {
            std::lock_guard lock(g_mutex);
            batch.swap(g_pending);
        }
        if (batch.empty()) continue;
        std::fwrite(batch.data(), 1, batch.size(), g_file);
        std::fflush(g_file);
        batch.clear();
    }
}

}  // namespace

namespace capture {

bool start() {
    SYSTEMTIME now;
    GetLocalTime(&now);
    wchar_t name[64];
    swprintf_s(name, L"\\capture-%04u%02u%02u-%02u%02u%02u.cdcap", now.wYear, now.wMonth, now.wDay, now.wHour,
               now.wMinute, now.wSecond);
    g_file = _wfsopen((coopDirectory() + name).c_str(), L"wb", _SH_DENYNO);
    if (!g_file) {
        logger::write("capture: cannot create the file, capture off");
        return false;
    }
    LARGE_INTEGER frequency;
    QueryPerformanceFrequency(&frequency);
    FileHeader header{};
    std::memcpy(header.magic, kMagic, sizeof(kMagic));
    header.exeTimeDateStamp = build_gate::kSupported.timeDateStamp;
    header.qpcFrequency = static_cast<uint64_t>(frequency.QuadPart);
    std::fwrite(&header, sizeof(header), 1, g_file);
    if (HANDLE thread = CreateThread(nullptr, 0, writerThread, nullptr, 0, nullptr)) CloseHandle(thread);
    logger::write("capture: writing %ls", name + 1);
    return true;
}

void record(packet::Direction direction, uint64_t session, uint64_t channel, uint64_t flags, const uint8_t* bytes,
            uint32_t size) {
    LARGE_INTEGER qpc;
    QueryPerformanceCounter(&qpc);
    Record header{static_cast<uint64_t>(qpc.QuadPart), GetCurrentThreadId(), static_cast<uint16_t>(size),
                  static_cast<uint8_t>(direction), static_cast<uint8_t>(session), static_cast<uint32_t>(channel),
                  static_cast<uint32_t>(flags)};
    std::lock_guard lock(g_mutex);
    if (g_pending.size() + sizeof(header) + size > kBufferLimit) {
        g_dropped.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    const auto* head = reinterpret_cast<const uint8_t*>(&header);
    g_pending.insert(g_pending.end(), head, head + sizeof(header));
    g_pending.insert(g_pending.end(), bytes, bytes + size);
}

uint64_t dropped() { return g_dropped.load(std::memory_order_relaxed); }

}  // namespace capture
