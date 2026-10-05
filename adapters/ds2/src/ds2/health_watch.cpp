// DEATH STRANDING 2: an in-process hardware write watch. Debug register 0 is set to a write breakpoint on 4 bytes on
// every thread of the process (a helper thread suspends each thread, sets Dr0 and Dr7 through SetThreadContext and
// resumes it); a vectored exception handler catches the single-step trap, records the writing instruction and the
// return addresses on the stack, clears Dr6 and lets the thread go on. The handler only writes into a fixed table; the
// simulation tick logs it. Nothing is attached from outside, so there is no debugger to leave armed when a tool dies.
#include "ds2/health_watch.h"

#include <windows.h>
#include <tlhelp32.h>

#include <algorithm>
#include <atomic>
#include <vector>

#include "decima/safe_read.h"
#include "ds2/engine.h"
#include "ds2/sim_tick.h"
#include "log.h"

namespace {

constexpr uint64_t kDr7Local0 = 1ull << 0;
constexpr uint64_t kDr7Write0 = 1ull << 16;  // RW0 = 01: break on data writes
constexpr uint64_t kDr7Len4 = 3ull << 18;    // LEN0 = 11: 4 bytes
constexpr uint64_t kDr7Mask = kDr7Local0 | (1ull << 1) | (3ull << 16) | (3ull << 18);
constexpr uint64_t kDr6Hit0 = 1;
constexpr size_t kMaxHits = 64;
constexpr size_t kReturns = 8;
constexpr size_t kStackWords = 192;
constexpr uintptr_t kImageBase = 0x140000000, kImageSpan = 0x20000000;
constexpr uint32_t kMaxTraps = 5000;
constexpr ULONGLONG kMaxWatchMs = 5 * 60 * 1000;
constexpr size_t kCallRel32 = 5, kCallIndirectRip = 6, kCallRegister = 2, kCallRegisterDisp = 3;
constexpr uint8_t kOpCallRel32 = 0xE8, kOpGroupFF = 0xFF;

struct Hit {
    std::atomic<uintptr_t> rip{0};  // set last: the entry is complete once it is non-zero
    float value = 0;
    DWORD thread = 0;
    std::atomic<uint32_t> repeats{0};
    uintptr_t returns[kReturns] = {};
};

Hit g_hits[kMaxHits];
std::atomic<size_t> g_hitCount{0};
std::atomic<uint32_t> g_traps{0};
std::atomic<uintptr_t> g_address{0};
PVOID g_handler = nullptr;
size_t g_logged = 0;
ULONGLONG g_armedAt = 0;

bool inImage(uintptr_t address) { return address >= ds2::at(kImageBase) && address < ds2::at(kImageBase) + kImageSpan; }

// An address in the image that follows a call instruction.
bool followsCall(uintptr_t address) {
    uint8_t before[kCallIndirectRip] = {};
    if (!decima::safeCopy(before, address - kCallIndirectRip, sizeof(before))) return false;
    return before[kCallIndirectRip - kCallRel32] == kOpCallRel32 || before[0] == kOpGroupFF ||
           before[kCallIndirectRip - kCallRegister] == kOpGroupFF || before[kCallIndirectRip - kCallRegisterDisp] == kOpGroupFF;
}

LONG CALLBACK onException(EXCEPTION_POINTERS* info) {
    if (info->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP) return EXCEPTION_CONTINUE_SEARCH;
    CONTEXT* context = info->ContextRecord;
    if (!(context->Dr6 & kDr6Hit0)) return EXCEPTION_CONTINUE_SEARCH;
    context->Dr6 = 0;
    ++g_traps;
    const uintptr_t rip = static_cast<uintptr_t>(context->Rip);
    const size_t known = std::min(g_hitCount.load(), kMaxHits);
    for (size_t i = 0; i < known; ++i) {
        if (g_hits[i].rip.load() == rip) {
            ++g_hits[i].repeats;
            return EXCEPTION_CONTINUE_EXECUTION;
        }
    }
    const size_t slot = g_hitCount.fetch_add(1);
    if (slot >= kMaxHits) return EXCEPTION_CONTINUE_EXECUTION;
    Hit& hit = g_hits[slot];
    hit.thread = GetCurrentThreadId();
    decima::safeRead(g_address.load(), hit.value);
    size_t found = 0;
    for (size_t word = 0; word < kStackWords && found < kReturns; ++word) {
        uintptr_t candidate = 0;
        if (decima::safeRead(static_cast<uintptr_t>(context->Rsp) + word * sizeof(uintptr_t), candidate) && inImage(candidate) &&
            followsCall(candidate)) {
            hit.returns[found++] = candidate;
        }
    }
    hit.repeats = 1;
    hit.rip = rip;
    return EXCEPTION_CONTINUE_EXECUTION;
}

struct Job {
    uintptr_t address;  // 0 clears
    std::vector<DWORD> threads;
};

DWORD WINAPI applyToThreads(LPVOID argument) {
    const Job& job = *static_cast<const Job*>(argument);
    for (const DWORD id : job.threads) {
        const HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_SET_CONTEXT, FALSE, id);
        if (!thread) continue;
        if (SuspendThread(thread) != static_cast<DWORD>(-1)) {
            CONTEXT context{};
            context.ContextFlags = CONTEXT_DEBUG_REGISTERS;
            if (GetThreadContext(thread, &context)) {
                context.Dr0 = job.address;
                context.Dr6 = 0;
                context.Dr7 = job.address ? (context.Dr7 | kDr7Local0 | kDr7Write0 | kDr7Len4) : (context.Dr7 & ~kDr7Mask);
                SetThreadContext(thread, &context);
            }
            ResumeThread(thread);
        }
        CloseHandle(thread);
    }
    return 0;
}

std::vector<DWORD> threadsOfProcess() {
    std::vector<DWORD> ids;
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return ids;
    THREADENTRY32 entry{};
    entry.dwSize = sizeof(entry);
    for (BOOL ok = Thread32First(snapshot, &entry); ok; ok = Thread32Next(snapshot, &entry)) {
        if (entry.th32OwnerProcessID == GetCurrentProcessId()) ids.push_back(entry.th32ThreadID);
    }
    CloseHandle(snapshot);
    return ids;
}

// Runs the register change on a helper thread (so the calling thread is one of the threads it can suspend) and waits.
void setOnAllThreads(uintptr_t address) {
    Job job{address, threadsOfProcess()};
    DWORD helperId = 0;
    const HANDLE helper = CreateThread(nullptr, 0, applyToThreads, &job, CREATE_SUSPENDED, &helperId);
    if (!helper) return;
    std::erase(job.threads, helperId);
    ResumeThread(helper);
    WaitForSingleObject(helper, INFINITE);
    CloseHandle(helper);
}

void logNewHits() {
    const size_t count = std::min(g_hitCount.load(), kMaxHits);
    for (; g_logged < count && g_hits[g_logged].rip.load(); ++g_logged) {
        const Hit& hit = g_hits[g_logged];
        logger::write("health_watch: write by %p (value %g, thread %lu); returns %p %p %p %p %p %p %p %p",
                      reinterpret_cast<void*>(hit.rip.load()), hit.value, hit.thread, reinterpret_cast<void*>(hit.returns[0]),
                      reinterpret_cast<void*>(hit.returns[1]), reinterpret_cast<void*>(hit.returns[2]),
                      reinterpret_cast<void*>(hit.returns[3]), reinterpret_cast<void*>(hit.returns[4]),
                      reinterpret_cast<void*>(hit.returns[5]), reinterpret_cast<void*>(hit.returns[6]),
                      reinterpret_cast<void*>(hit.returns[7]));
    }
}

void tick() {
    if (!g_address.load()) return;
    logNewHits();
    if (g_traps.load() >= kMaxTraps || GetTickCount64() - g_armedAt > kMaxWatchMs) health_watch::disarm();
}

}  // namespace

namespace health_watch {

void installEarly() { sim_tick::add(&tick, "health watch"); }

void arm(uintptr_t address) {
    disarm();
    if (!address) return;
    if (!g_handler) g_handler = AddVectoredExceptionHandler(1, onException);
    for (Hit& hit : g_hits) {
        hit.rip = 0;
        hit.repeats = 0;
    }
    g_hitCount = 0;
    g_traps = 0;
    g_logged = 0;
    g_address = address;
    g_armedAt = GetTickCount64();
    setOnAllThreads(address);
    float value = 0;
    decima::safeRead(address, value);
    logger::write("health_watch: armed on %p (value %g)", reinterpret_cast<void*>(address), value);
}

void disarm() {
    if (!g_address.load()) return;
    setOnAllThreads(0);
    logNewHits();
    for (size_t i = 0; i < std::min(g_hitCount.load(), kMaxHits); ++i) {
        logger::write("health_watch: %p wrote %u times", reinterpret_cast<void*>(g_hits[i].rip.load()), g_hits[i].repeats.load());
    }
    logger::write("health_watch: disarmed after %u writes", g_traps.load());
    g_address = 0;
}

}  // namespace health_watch
