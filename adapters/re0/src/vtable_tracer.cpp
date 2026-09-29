#include "vtable_tracer.h"

#include <windows.h>

#include <atomic>
#include <cstring>
#include <memory>

#include "game.h"
#include "log.h"
#include "thunk_emit.h"

namespace {

constexpr size_t kThunkSize = 32;
constexpr DWORD kReportIntervalMs = 2000;
constexpr size_t kPointerSize = sizeof(uint32_t);

struct SlotStats {
    volatile LONG count;
    volatile uint32_t lastReturn;
    volatile uint32_t lastThis;
    uint32_t original;
};

struct TracedVtable {
    uintptr_t address;
    unsigned slots;
    uint8_t* thunks;
    std::unique_ptr<SlotStats[]> stats;
    std::unique_ptr<LONG[]> reported;
};

std::vector<TracedVtable> g_vtables;
std::atomic<bool> g_reporting{false};

// Transparent to thiscall/stdcall/cdecl: only eax and flags are touched, then a jump to the original.
void emitThunk(uint8_t* p, SlotStats& s) {
    thunk::emit(p, {0xF0, 0xFF, 0x05});  // lock inc dword [count]
    thunk::emitAddress(p, &s.count);
    thunk::emit(p, {0x8B, 0x04, 0x24});  // mov eax, [esp]
    thunk::emit(p, {0xA3});              // mov [lastReturn], eax
    thunk::emitAddress(p, &s.lastReturn);
    thunk::emit(p, {0x89, 0x0D});  // mov [lastThis], ecx
    thunk::emitAddress(p, &s.lastThis);
    thunk::emit(p, {0xFF, 0x25});  // jmp [original]
    thunk::emitAddress(p, &s.original);
}

bool patch(TracedVtable& vt) {
    vt.thunks = static_cast<uint8_t*>(
        VirtualAlloc(nullptr, vt.slots * kThunkSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!vt.thunks) return false;
    vt.stats = std::make_unique<SlotStats[]>(vt.slots);
    vt.reported = std::make_unique<LONG[]>(vt.slots);

    for (unsigned i = 0; i < vt.slots; ++i) {
        const uintptr_t slotAddress = vt.address + i * kPointerSize;
        const uintptr_t original = game::readPointer(slotAddress);
        if (!original) continue;
        vt.stats[i].original = static_cast<uint32_t>(original);
        emitThunk(vt.thunks + i * kThunkSize, vt.stats[i]);
    }
    FlushInstructionCache(GetCurrentProcess(), vt.thunks, vt.slots * kThunkSize);

    for (unsigned i = 0; i < vt.slots; ++i) {
        if (vt.stats[i].original) {
            game::writeProtected(vt.address + i * kPointerSize, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(vt.thunks + i * kThunkSize)));
        }
    }
    return true;
}

void report(TracedVtable& vt) {
    for (unsigned i = 0; i < vt.slots; ++i) {
        const LONG count = vt.stats[i].count;
        if (count == vt.reported[i]) continue;
        logger::write("trace vtable=0x%x slot=%u calls/2s=%ld ret=0x%x this=0x%x", static_cast<unsigned>(vt.address), i,
                      count - vt.reported[i], vt.stats[i].lastReturn, vt.stats[i].lastThis);
        vt.reported[i] = count;
    }
}

DWORD WINAPI reporterThread(LPVOID) {
    while (g_reporting) {
        Sleep(kReportIntervalMs);
        for (auto& vt : g_vtables) report(vt);
    }
    return 0;
}

}  // namespace

namespace vtable_tracer {

bool install(const std::vector<VtableTrace>& vtables) {
    g_vtables.reserve(vtables.size());
    for (const auto& request : vtables) {
        TracedVtable vt{request.address, request.count, nullptr, nullptr, nullptr};
        if (!patch(vt)) {
            logger::write("trace: failed to patch vtable 0x%x", static_cast<unsigned>(request.address));
            continue;
        }
        logger::write("trace: patched vtable 0x%x, %u slots", static_cast<unsigned>(request.address), request.count);
        g_vtables.push_back(std::move(vt));
    }
    if (g_vtables.empty()) return false;
    g_reporting = true;
    if (HANDLE thread = CreateThread(nullptr, 0, reporterThread, nullptr, 0, nullptr)) CloseHandle(thread);
    return true;
}

void uninstall() {
    g_reporting = false;
    for (auto& vt : g_vtables) {
        for (unsigned i = 0; i < vt.slots; ++i) {
            const uintptr_t slotAddress = vt.address + i * kPointerSize;
            const uint32_t thunk = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(vt.thunks + i * kThunkSize));
            if (vt.stats[i].original && game::readPointer(slotAddress) == thunk) game::writeProtected(slotAddress, vt.stats[i].original);
        }
    }
}

}  // namespace vtable_tracer
